#include "test_paths.h"

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/playback/playback_clock.h"
#include "abrviz/core/render/render_plan.h"

#include <QDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

using namespace abrviz;
using Piece = PlaybackClock::Piece;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kOnScreen = 3.267;  ///< first to last frame of a fully played segment
constexpr double kFrame = 0.033;     ///< the clock's frame duration
constexpr std::uint32_t kSeed = 20261114;

/// Equal within `tolerance`, relative for large values.
bool near(double a, double b, double tolerance = 1e-9)
{
    return std::abs(a - b) <= tolerance * std::max({1.0, std::abs(a), std::abs(b)});
}

QString num(double value)
{
    return QString::number(value, 'g', 17);
}

/// A run with `count` downloaded segments of `duration` s of media and a one-rung ladder.
/// Nothing is played.
RunData downloadedRun(int count, double duration = 2.0)
{
    RunData run;
    run.info.runId = QStringLiteral("edge");
    run.video.segmentCount = count;
    run.video.ladder = {{0, 1'000'000, 640, 360}};
    for (int i = 0; i < count; ++i) {
        Segment s;
        s.index = i;
        s.mediaDuration = duration;
        s.mediaPath = QStringLiteral("/media/seg_%1.mp4").arg(i);
        run.segments.push_back(s);
    }
    return run;
}

/// Shows `segment` from `first` for 3.3 s.
void play(RunData& run, int segment, double first, int frames = kFramesPerSegment)
{
    run.playback.push_back({segment, first, first + kOnScreen, frames, 1'000'000});
}

/// `count` segments of 2 s media played back to back from 1.0 s, every frame shown.
RunData straightRun(int count)
{
    RunData run = downloadedRun(count);
    for (int i = 0; i < count; ++i)
        play(run, i, 1.0 + 3.3 * i);
    return run;
}

/// Checks the pieces: finite, in simulation order without overlaps, contiguous in media and
/// replay time, and the totals add up. Returns a description of the first problem, or nothing.
QString pieceProblem(const PlaybackClock& clock)
{
    const auto pieces = clock.pieces();
    if (pieces.empty()) {
        if (clock.replayDuration() != 0.0 || clock.mediaDuration() != 0.0
            || clock.simStart() != 0.0 || clock.simEnd() != 0.0)
            return QStringLiteral("empty clock with non-zero totals");
        return {};
    }
    double stalled = 0.0;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& p = pieces[i];
        const QString at = QStringLiteral("piece %1: ").arg(i);
        for (double v : {p.simStart, p.simEnd, p.mediaStart, p.mediaEnd, p.replayStart, p.replayEnd}) {
            if (!std::isfinite(v))
                return at + QStringLiteral("non-finite time");
        }
        if (p.simEnd < p.simStart || p.mediaEnd < p.mediaStart || p.replayEnd < p.replayStart)
            return at + QStringLiteral("ends before it starts");
        const double replayStart = i == 0 ? 0.0 : pieces[i - 1].replayEnd;
        const double mediaStart = i == 0 ? 0.0 : pieces[i - 1].mediaEnd;
        if (p.replayStart != replayStart)
            return at + QStringLiteral("replay gap or overlap: %1 vs %2").arg(num(p.replayStart), num(replayStart));
        if (p.mediaStart != mediaStart)
            return at + QStringLiteral("media gap or overlap");
        if (i > 0 && p.simStart < pieces[i - 1].simStart)
            return at + QStringLiteral("not in simulation order");
        if (i > 0 && pieces[i - 1].simEnd > p.simStart)
            return at + QStringLiteral("overlaps the previous piece in simulation time: %1 > %2")
                            .arg(num(pieces[i - 1].simEnd), num(p.simStart));
        if (p.kind == Piece::Kind::Stall) {
            if (p.mediaEnd != p.mediaStart)
                return at + QStringLiteral("stall takes media time");
            if (i == 0)
                return at + QStringLiteral("stall before any segment was shown");
            stalled += p.replayEnd - p.replayStart;
        } else if (std::abs((p.replayEnd - p.replayStart) - (p.mediaEnd - p.mediaStart))
                   > 1e-9 * std::max(1.0, p.replayEnd)) {  // rounding grows with the replay time
            return at + QStringLiteral("segment replay and media lengths differ");
        }
    }
    if (!near(clock.replayDuration(), clock.mediaDuration() + stalled))
        return QStringLiteral("replay %1 != media %2 + stalls %3")
            .arg(num(clock.replayDuration()), num(clock.mediaDuration()), num(stalled));
    if (clock.replayDuration() != pieces.back().replayEnd || clock.simStart() != pieces.front().simStart
        || clock.simEnd() != pieces.back().simEnd)
        return QStringLiteral("totals disagree with the pieces");
    return {};
}

/// Sweeps replay and simulation time in `steps` steps (plus a margin either side) and checks
/// that every result is finite, inside the session and never goes backwards.
QString sweepProblem(const PlaybackClock& clock, int steps = 2000)
{
    const double duration = clock.replayDuration();
    const double step = std::max(duration / steps, 1e-3);
    double previousSim = -kInf;
    double previousMedia = -kInf;
    for (int i = -20; i <= steps + 20; ++i) {
        const double r = i * step;
        const double sim = clock.replayToSim(r);
        const double media = clock.replayToMedia(r);
        const PlaybackClock::Position pos = clock.positionAtReplay(r);
        const QString at = QStringLiteral("replay %1: ").arg(num(r));
        if (!std::isfinite(sim) || !std::isfinite(media) || !std::isfinite(pos.fraction))
            return at + QStringLiteral("non-finite result");
        if (sim < previousSim)
            return at + QStringLiteral("simulation time goes backwards: %1 < %2").arg(num(sim), num(previousSim));
        if (media < previousMedia)
            return at + QStringLiteral("media time goes backwards");
        previousSim = sim;
        previousMedia = media;
        if (sim < clock.simStart() || sim > clock.simEnd() || media < 0.0 || media > clock.mediaDuration())
            return at + QStringLiteral("outside the session");
        if (pos.fraction < 0.0 || pos.fraction > 1.0)
            return at + QStringLiteral("fraction %1").arg(num(pos.fraction));
        if (clock.isEmpty()) {
            if (!pos.beforeStart || sim != 0.0 || media != 0.0)
                return at + QStringLiteral("empty clock gave a position");
            continue;
        }
        if (pos.beforeStart != (r < 0.0) || pos.afterEnd != (r >= duration))
            return at + QStringLiteral("wrong before/after flags");
    }

    const double simStep = std::max((clock.simEnd() - clock.simStart()) / steps, 1e-3);
    double previousReplay = -kInf;
    for (int i = -20; i <= steps + 20; ++i) {
        const double s = clock.simStart() + i * simStep;
        const double r = clock.simToReplay(s);
        const PlaybackClock::Position pos = clock.positionAtSim(s);
        if (!std::isfinite(r) || r < previousReplay || r < 0.0 || r > duration)
            return QStringLiteral("sim %1: replay %2 after %3").arg(num(s), num(r), num(previousReplay));
        if (!std::isfinite(pos.fraction) || pos.fraction < 0.0 || pos.fraction > 1.0)
            return QStringLiteral("sim %1: bad fraction").arg(num(s));
        previousReplay = r;
    }
    return {};
}

/// Checks the conversions at and inside every piece: exact starts, and round trips between
/// replay and simulation time and between replay and media time.
QString boundaryProblem(const PlaybackClock& clock)
{
    const auto pieces = clock.pieces();
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const Piece& p = pieces[i];
        const QString at = QStringLiteral("piece %1: ").arg(i);
        const double replayLength = p.replayEnd - p.replayStart;
        const double simLength = p.simEnd - p.simStart;
        const double mediaLength = p.mediaEnd - p.mediaStart;
        const bool stall = p.kind == Piece::Kind::Stall;
        if (replayLength > 0.0) {
            // Exactly at the start the piece is on screen, at its first frame.
            const auto pos = clock.positionAtReplay(p.replayStart);
            if (pos.segment != p.segment || pos.stalled != stall || pos.fraction != (stall ? 1.0 : 0.0))
                return at + QStringLiteral("wrong position at its replay start");
            if (clock.replayToSim(p.replayStart) != p.simStart)
                return at + QStringLiteral("replay start does not map to its simulation start");
            if (clock.replayToMedia(p.replayStart) != p.mediaStart)
                return at + QStringLiteral("replay start does not map to its media start");
            // Just before the end it still is, and almost at its last frame.
            const double justBefore = std::nextafter(p.replayEnd, -kInf);
            if (justBefore > p.replayStart) {
                const auto last = clock.positionAtReplay(justBefore);
                if (last.segment != p.segment || last.stalled != stall || last.fraction < 1.0 - 1e-6)
                    return at + QStringLiteral("wrong position just before its replay end");
            }
        }
        for (double f : {0.001, 0.25, 0.5, 0.75, 0.999}) {
            const double r = p.replayStart + f * replayLength;
            if (replayLength > 1e-6 && simLength > 1e-6) {
                const double back = clock.simToReplay(clock.replayToSim(r));
                if (!near(back, r, 1e-6))
                    return at + QStringLiteral("replay %1 -> sim -> replay %2").arg(num(r), num(back));
                const auto pos = clock.positionAtSim(clock.replayToSim(r));
                if (pos.segment != p.segment || pos.stalled != stall)
                    return at + QStringLiteral("positionAtSim disagrees with positionAtReplay");
            }
            if (!stall && mediaLength > 1e-6) {
                const double back = clock.mediaToReplay(clock.replayToMedia(r));
                if (!near(back, r, 1e-6))
                    return at + QStringLiteral("replay %1 -> media -> replay %2").arg(num(r), num(back));
                const auto pos = clock.positionAtReplay(r);
                if (!near(pos.fraction, (r - p.replayStart) / replayLength, 1e-6))
                    return at + QStringLiteral("fraction %1 at %2").arg(num(pos.fraction), num(f));
            }
        }
    }
    return {};
}

/// Checks that the render plan follows the clock: one part per known segment piece, every
/// stall a freeze, the same length as the replay.
QString planProblem(const RunData& run, const PlaybackClock& clock)
{
    const RenderPlan plan = makeRenderPlan(run);
    if (!std::isfinite(plan.frameRate) || plan.frameRate <= 0.0)
        return QStringLiteral("bad frame rate");
    std::size_t parts = 0;
    for (const Piece& p : clock.pieces()) {
        if (p.kind == Piece::Kind::Segment
            && std::ranges::find(run.segments, p.segment, &Segment::index) != run.segments.end())
            ++parts;
    }
    if (plan.parts.size() != parts)
        return QStringLiteral("%1 parts for %2 segment pieces").arg(plan.parts.size()).arg(parts);
    for (const RenderPart& part : plan.parts) {
        if (!std::isfinite(part.duration) || part.duration < 0.0 || !std::isfinite(part.freezeAfter)
            || part.freezeAfter < 0.0)
            return QStringLiteral("bad part for segment %1").arg(part.segment);
    }
    if (!plan.parts.empty() && !near(plan.duration(), clock.replayDuration()))
        return QStringLiteral("plan %1 s, replay %2 s").arg(num(plan.duration()), num(clock.replayDuration()));
    return {};
}

/// Every check on the clock of `run`; empty if all pass.
QString allProblems(const RunData& run, int steps = 2000)
{
    const PlaybackClock clock(run);
    for (const QString& problem : {pieceProblem(clock), sweepProblem(clock, steps),
                                   boundaryProblem(clock), planProblem(run, clock)}) {
        if (!problem.isEmpty())
            return problem;
    }
    return {};
}

#define CHECK_RUN(run) \
    do { \
        const QString problem_ = allProblems(run); \
        QVERIFY2(problem_.isEmpty(), qPrintable(problem_)); \
    } while (false)

/// A plausible run: segments in order, some skipped, stalls between them, maybe a terminal
/// stall and a cut-off last segment.
RunData randomValidRun(std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const int count = std::uniform_int_distribution<int>(0, 12)(rng);
    RunData run = downloadedRun(count);
    for (Segment& s : run.segments)
        s.mediaDuration = unit(rng) < 0.1 ? 0.0 : 0.5 + 2.5 * unit(rng);
    run.video.ladder = {{0, 250'000, 428, 240}, {1, 750'000, 640, 360}, {2, 1'500'000, 854, 480}};
    for (Segment& s : run.segments)
        s.playedRung = std::uniform_int_distribution<int>(0, 2)(rng);

    double t = -5.0 + 55.0 * unit(rng);
    run.playbackStart = t;
    for (int i = 0; i < count; ++i) {
        if (unit(rng) < 0.1)
            continue;  // skipped
        if (unit(rng) < 0.3) {
            const double length = unit(rng) < 0.1 ? 0.0 : 5.0 * unit(rng);
            run.stalls.push_back({t, t + length, false});
            t += length;
        }
        play(run, i, t);
        t += 0.1 + 4.0 * unit(rng);
    }
    if (unit(rng) < 0.2) {
        run.stalls.push_back({t, t + 3.0 * unit(rng), true});
    }
    if (!run.playback.empty() && unit(rng) < 0.3)
        run.playback.back().framesPlayed = std::uniform_int_distribution<int>(0, 100)(rng);
    return run;
}

/// Anything: unknown segments, times out of order, overlapping stalls, NaN and infinities.
RunData randomNastyRun(std::mt19937& rng)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto time = [&] {
        const double pick = unit(rng);
        if (pick < 0.05)
            return kNaN;
        if (pick < 0.08)
            return kInf;
        if (pick < 0.11)
            return -kInf;
        if (pick < 0.15)
            return 1e9 + unit(rng);
        if (pick < 0.2)
            return 0.0;
        return -10.0 + 40.0 * unit(rng);
    };
    const int count = std::uniform_int_distribution<int>(0, 6)(rng);
    RunData run = downloadedRun(count);
    for (Segment& s : run.segments) {
        const double pick = unit(rng);
        s.mediaDuration = pick < 0.1 ? kNaN : pick < 0.2 ? -1.0 : pick < 0.25 ? kInf : 3.0 * unit(rng);
    }
    const int records = std::uniform_int_distribution<int>(0, 8)(rng);
    for (int i = 0; i < records; ++i) {
        const int segment = std::uniform_int_distribution<int>(-2, count + 2)(rng);
        const double first = time();
        const double last = unit(rng) < 0.5 ? first + 3.267 : time();
        run.playback.push_back({segment, first, last, std::uniform_int_distribution<int>(-10, 200)(rng), 0});
    }
    const int stalls = std::uniform_int_distribution<int>(0, 5)(rng);
    for (int i = 0; i < stalls; ++i) {
        const double start = time();
        const double end = unit(rng) < 0.5 ? start + 2.0 * unit(rng) : time();
        run.stalls.push_back({start, end, false});
    }
    return run;
}

} // namespace

class TestPlaybackEdgeCases : public QObject {
    Q_OBJECT

private slots:
    void emptyClockIsInertForEveryInput()
    {
        const PlaybackClock empty;
        QVERIFY(empty.isEmpty());
        for (double t : {-1.0, 0.0, 1.0, 1e9, kNaN, kInf, -kInf}) {
            QCOMPARE(empty.simToReplay(t), 0.0);
            QCOMPARE(empty.replayToSim(t), 0.0);
            QCOMPARE(empty.replayToMedia(t), 0.0);
            QCOMPARE(empty.mediaToReplay(t), 0.0);
            QVERIFY(empty.positionAtSim(t).beforeStart);
            QVERIFY(empty.positionAtReplay(t).beforeStart);
            QCOMPARE(empty.positionAtReplay(t).segment, -1);
        }
        QVERIFY(pieceProblem(empty).isEmpty());

        // Segments downloaded but none played.
        const RunData run = downloadedRun(5);
        QVERIFY(PlaybackClock(run).isEmpty());
        QVERIFY(!run.playbackCutOff());
        QVERIFY(makeRenderPlan(run).parts.empty());
        QCOMPARE(makeRenderPlan(run).duration(), 0.0);
    }

    void stallsWithoutPlaybackAreNotReplayed()
    {
        // Nothing was ever on screen, so there is no frame to freeze.
        RunData run = downloadedRun(3);
        run.stalls = {{1.0, 4.0, false}, {5.0, 9.0, true}};
        const PlaybackClock clock(run);
        QVERIFY2(clock.isEmpty(), "stalls before the first frame make replay time");
        QVERIFY(clock.positionAtReplay(1.0).beforeStart);
        CHECK_RUN(run);
    }

    void playbackOfUnknownSegments()
    {
        // Playback records for segments that were never downloaded: no media, no NaN.
        RunData run = downloadedRun(0);
        play(run, 0, 1.0);
        play(run, 7, 4.3);
        play(run, -1, 7.6);
        const PlaybackClock clock(run);
        QCOMPARE(clock.pieces().size(), 3u);
        QCOMPARE(clock.replayDuration(), 0.0);
        QCOMPARE(clock.simToReplay(5.0), 0.0);
        QCOMPARE(clock.positionAtSim(5.0).segment, 7);
        QVERIFY(clock.positionAtReplay(0.0).afterEnd);  // a zero-length replay is over at 0
        CHECK_RUN(run);
        QVERIFY(makeRenderPlan(run).parts.empty());

        const QoeReport qoe = computeQoe(run);
        QCOMPARE(qoe.segmentsShown, 0);
        QCOMPARE(qoe.averagePlayedBitrate, 0.0);
        QCOMPARE(qoe.lastSegmentShown, -1);
    }

    void singleSegmentBoundaries()
    {
        RunData run = straightRun(1);
        const PlaybackClock clock(run);
        QCOMPARE(clock.pieces().size(), 1u);
        QCOMPARE(clock.simStart(), 1.0);
        QVERIFY(near(clock.simEnd(), 1.0 + kOnScreen + kFrame));
        QCOMPARE(clock.replayDuration(), 2.0);
        QCOMPARE(clock.mediaDuration(), 2.0);

        // Before the start everything is clamped to the start.
        QCOMPARE(clock.simToReplay(-100.0), 0.0);
        QCOMPARE(clock.simToReplay(std::nextafter(1.0, 0.0)), 0.0);
        QVERIFY(clock.positionAtSim(std::nextafter(1.0, 0.0)).beforeStart);
        QVERIFY(!clock.positionAtSim(1.0).beforeStart);
        QCOMPARE(clock.replayToSim(-1.0), 1.0);
        QVERIFY(clock.positionAtReplay(-1e-12).beforeStart);
        QCOMPARE(clock.positionAtReplay(0.0).segment, 0);
        QCOMPARE(clock.positionAtReplay(0.0).fraction, 0.0);

        // At and after the end: the last frame, flagged as the end.
        QCOMPARE(clock.replayToSim(2.0), clock.simEnd());
        QCOMPARE(clock.replayToSim(1e9), clock.simEnd());
        const auto end = clock.positionAtReplay(2.0);
        QVERIFY(end.afterEnd);
        QCOMPARE(end.segment, 0);
        QCOMPARE(end.fraction, 1.0);
        const auto justBefore = clock.positionAtReplay(std::nextafter(2.0, 0.0));
        QVERIFY(!justBefore.afterEnd);
        QVERIFY(justBefore.fraction > 0.999999 && justBefore.fraction < 1.0);
        QVERIFY(clock.positionAtSim(clock.simEnd()).afterEnd);
        QVERIFY(!clock.positionAtSim(std::nextafter(clock.simEnd(), 0.0)).afterEnd);
        QCOMPARE(clock.mediaToReplay(1e9), 2.0);
        QCOMPARE(clock.mediaToReplay(-1.0), 0.0);
        CHECK_RUN(run);
    }

    void exactSegmentAndStallBoundaries()
    {
        // Segments at 1.0 and 4.3, a stall from 7.6 to 8.0, the third segment at 8.0.
        RunData run = straightRun(3);
        run.playback[2] = {2, 8.0, 8.0 + kOnScreen, 100, 1'000'000};
        run.stalls = {{7.6, 8.0, false}};
        const PlaybackClock clock(run);
        QCOMPARE(clock.pieces().size(), 4u);
        // Replay: [0,2) seg 0, [2,4) seg 1, [4,4.4) stall, [4.4,6.4) seg 2.
        const struct {
            double replay;
            int segment;
            bool stalled;
            double fraction;
        } expected[] = {
            {0.0, 0, false, 0.0},           {std::nextafter(2.0, 0.0), 0, false, 1.0},
            {2.0, 1, false, 0.0},           {std::nextafter(4.0, 0.0), 1, false, 1.0},
            {4.0, 1, true, 1.0},            {std::nextafter(4.4, 0.0), 1, true, 1.0},
            {4.4, 2, false, 0.0},           {6.4, 2, false, 1.0},
        };
        for (const auto& e : expected) {
            const auto pos = clock.positionAtReplay(e.replay);
            const QByteArray at = QByteArray::number(e.replay, 'g', 17);
            QVERIFY2(pos.segment == e.segment, at);
            QVERIFY2(pos.stalled == e.stalled, at);
            QVERIFY2(near(pos.fraction, e.fraction, 1e-6), at);
        }
        // Simulation boundaries.
        QCOMPARE(clock.simToReplay(4.3), 2.0);
        QCOMPARE(clock.simToReplay(7.6), 4.0);
        QVERIFY(near(clock.simToReplay(8.0), 4.4));
        QVERIFY(clock.positionAtSim(7.6).stalled);
        QVERIFY(!clock.positionAtSim(8.0).stalled);
        QVERIFY(!clock.positionAtSim(std::nextafter(7.6, 0.0)).stalled);
        QVERIFY(clock.positionAtSim(std::nextafter(8.0, 0.0)).stalled);
        // Media time stops during the stall; at the stall's media time the segment after wins.
        QCOMPARE(clock.replayToMedia(4.2), 4.0);
        QVERIFY(near(clock.mediaToReplay(4.0), 4.4));
        QVERIFY(near(clock.mediaToReplay(std::nextafter(4.0, 0.0)), 4.0, 1e-12));
        QCOMPARE(clock.replayToSim(4.0), 7.6);
        QVERIFY(near(clock.replayToSim(4.4), 8.0));
        CHECK_RUN(run);
    }

    void zeroLengthSegments()
    {
        // The middle segment has no media: it takes no replay time and never shows.
        RunData run = straightRun(3);
        run.segments[1].mediaDuration = 0.0;
        const PlaybackClock clock(run);
        QCOMPARE(clock.pieces()[1].replayStart, clock.pieces()[1].replayEnd);
        QCOMPARE(clock.replayDuration(), 4.0);
        QCOMPARE(clock.positionAtReplay(2.0).segment, 2);
        QCOMPARE(clock.positionAtReplay(std::nextafter(2.0, 0.0)).segment, 0);
        // In simulation time it was on screen, so the position reports it.
        const auto shown = clock.positionAtSim(5.0);
        QCOMPARE(shown.segment, 1);
        QVERIFY(near(shown.fraction, 0.7 / 3.3));
        QCOMPARE(clock.simToReplay(5.0), 2.0);
        CHECK_RUN(run);

        // Every segment empty: a replay of length 0.
        RunData all = straightRun(3);
        for (Segment& s : all.segments)
            s.mediaDuration = 0.0;
        const PlaybackClock none(all);
        QCOMPARE(none.replayDuration(), 0.0);
        QVERIFY(none.positionAtReplay(0.0).afterEnd);
        QCOMPARE(none.replayToSim(0.0), none.simEnd());
        CHECK_RUN(all);
    }

    void zeroLengthAndBackToBackStalls()
    {
        RunData run = straightRun(3);
        run.playback[2] = {2, 9.0, 9.0 + kOnScreen, 100, 1'000'000};
        // A zero-length stall at 7.6, then two stalls back to back until the next frame.
        run.stalls = {{7.6, 7.6, false}, {7.6, 8.2, false}, {8.2, 9.0, false}};
        const PlaybackClock clock(run);
        QCOMPARE(clock.pieces().size(), 6u);
        QVERIFY(near(clock.replayDuration(), 6.0 + 1.4));
        QCOMPARE(clock.positionAtReplay(4.0).stalled, true);
        QVERIFY(near(clock.simToReplay(8.2), 4.6));
        QVERIFY(near(clock.replayToSim(4.6), 8.2));
        QCOMPARE(computeQoe(run).stallCount, 3);
        QVERIFY(near(computeQoe(run).rebufferTime, 1.4));

        const RenderPlan plan = makeRenderPlan(run);
        QCOMPARE(plan.parts.size(), 3u);
        QVERIFY(near(plan.parts[1].freezeAfter, 1.4));  // all three freeze segment 1
        CHECK_RUN(run);
    }

    void stallBeforeTheFirstFrameIsNotReplayed()
    {
        // A stall before anything is on screen is start-up, not a freeze: the render plan drops
        // it, so the clock must too or the file's time and the replay time drift apart.
        for (const Stall& stall : {Stall{0.2, 0.8, false}, Stall{0.5, 1.5, false}, Stall{-3.0, 0.0, false}}) {
            RunData run = straightRun(3);
            run.stalls = {stall};
            const PlaybackClock clock(run);
            QVERIFY2(near(clock.replayDuration(), 6.0), qPrintable(num(clock.replayDuration())));
            QCOMPARE(clock.positionAtReplay(0.0).segment, 0);
            QVERIFY(!clock.positionAtReplay(0.0).stalled);
            QCOMPARE(clock.simToReplay(stall.start), 0.0);
            QVERIFY(near(makeRenderPlan(run).duration(), clock.replayDuration()));
            CHECK_RUN(run);
        }
        // A stall starting at the first frame's time sorts after it and freezes segment 0.
        RunData run = straightRun(3);
        run.stalls = {{1.0, 1.5, false}};
        const PlaybackClock clock(run);
        QVERIFY(near(clock.replayDuration(), 6.5));
        QVERIFY(clock.pieces()[1].kind == Piece::Kind::Stall);
        QCOMPARE(clock.pieces()[1].segment, 0);
        CHECK_RUN(run);
    }

    void terminalStallAndStallsPastTheEnd()
    {
        RunData run = straightRun(3);
        run.video.segmentCount = 4;
        run.stalls = {{10.9, 20.0, true}};
        const PlaybackClock clock(run);
        QVERIFY(near(clock.replayDuration(), 6.0 + 9.1));
        QCOMPARE(clock.simEnd(), 20.0);
        const auto frozen = clock.positionAtReplay(std::nextafter(clock.replayDuration(), 0.0));
        QVERIFY(frozen.stalled);
        QCOMPARE(frozen.segment, 2);
        QCOMPARE(frozen.fraction, 1.0);
        const auto end = clock.positionAtReplay(clock.replayDuration());
        QVERIFY(end.afterEnd);
        QVERIFY(end.stalled);
        // Media ends before the stall, so the end of the media maps to the end of the replay.
        QVERIFY(near(clock.mediaToReplay(6.0), clock.replayDuration()));
        QVERIFY(near(makeRenderPlan(run).parts.back().freezeAfter, 9.1));
        CHECK_RUN(run);

        // A stall long after the last frame, with a gap of nothing in between.
        RunData late = straightRun(3);
        late.stalls = {{100.0, 101.0, false}};
        const PlaybackClock lateClock(late);
        QVERIFY(near(lateClock.replayDuration(), 7.0));
        // The last segment lasts until the stall starts, so the gap stretches its media.
        QVERIFY(lateClock.simToReplay(50.0) > 4.0 && lateClock.simToReplay(50.0) < 6.0);
        QCOMPARE(lateClock.simEnd(), 101.0);
        CHECK_RUN(late);
    }

    void overlappingStallsKeepSimulationTimeMonotonic()
    {
        RunData run = straightRun(3);
        run.playback[2] = {2, 9.0, 9.0 + kOnScreen, 100, 1'000'000};
        run.stalls = {{7.6, 8.5, false}, {8.0, 9.0, false}};
        const PlaybackClock clock(run);
        // Replay time keeps both stalls in full (it matches the exported rebuffer time).
        QVERIFY(near(clock.replayDuration(), 6.0 + 0.9 + 1.0));
        QVERIFY(near(computeQoe(run).rebufferTime, 1.9));
        CHECK_RUN(run);

        // A stall that ends after the next segment's first frame.
        RunData late = straightRun(3);
        late.stalls = {{4.25, 5.0, false}};
        CHECK_RUN(late);
        const PlaybackClock lateClock(late);
        QCOMPARE(lateClock.positionAtSim(4.6).segment, 1);
        QVERIFY(!lateClock.positionAtSim(4.6).stalled);
    }

    void playbackOutOfOrderAndDuplicates()
    {
        RunData sorted = straightRun(4);
        sorted.stalls = {{7.6, 8.0, false}};
        RunData reversed = sorted;
        std::ranges::reverse(reversed.playback);
        const PlaybackClock a(sorted);
        const PlaybackClock b(reversed);
        QCOMPARE(a.pieces().size(), b.pieces().size());
        for (std::size_t i = 0; i < a.pieces().size(); ++i) {
            QCOMPARE(a.pieces()[i].segment, b.pieces()[i].segment);
            QCOMPARE(a.pieces()[i].simStart, b.pieces()[i].simStart);
        }
        CHECK_RUN(reversed);

        // The same segment shown twice (and two records at the same time).
        RunData dup = straightRun(2);
        play(dup, 1, 7.6);
        play(dup, 1, 7.6);
        const PlaybackClock clock(dup);
        QCOMPARE(clock.pieces().size(), 4u);
        QCOMPARE(clock.replayDuration(), 8.0);
        QCOMPARE(makeRenderPlan(dup).parts.size(), 4u);
        CHECK_RUN(dup);
    }

    void cutOffAtEveryFrameCount()
    {
        for (int frames : {-5, 0, 1, 50, 99, 100, 101, 1000}) {
            RunData run = straightRun(3);
            run.playback.back().framesPlayed = frames;
            const PlaybackClock clock(run);
            const double fraction = std::clamp(frames / 100.0, 0.0, 1.0);
            QVERIFY2(near(clock.mediaDuration(), 4.0 + 2.0 * fraction), qPrintable(QString::number(frames)));
            QCOMPARE(run.playbackCutOff(), frames < 100);
            CHECK_RUN(run);
        }
        // The last segment shown is not the video's last: cut off, but it played whole.
        RunData early = straightRun(3);
        early.video.segmentCount = 10;
        QVERIFY(early.playbackCutOff());
        QCOMPARE(PlaybackClock(early).mediaDuration(), 6.0);
        // Only the last record is scaled, never an earlier one with few frames.
        RunData earlier = straightRun(3);
        earlier.playback[1].framesPlayed = 10;
        QCOMPARE(PlaybackClock(earlier).mediaDuration(), 6.0);
    }

    void negativeAndHugeTimes()
    {
        // Simulation times before 0: replay time is still from 0.
        RunData negative = downloadedRun(3);
        play(negative, 0, -20.0);
        play(negative, 1, -16.7);
        play(negative, 2, -12.0);
        negative.stalls = {{-13.4, -12.0, false}};
        const PlaybackClock clock(negative);
        QCOMPARE(clock.simStart(), -20.0);
        QVERIFY(near(clock.replayDuration(), 6.0 + 1.4));
        QCOMPARE(clock.simToReplay(-20.0), 0.0);
        CHECK_RUN(negative);

        // Simulation times around 1e9: the mapping keeps sub-microsecond precision.
        RunData huge = downloadedRun(3);
        for (int i = 0; i < 3; ++i)
            play(huge, i, 1e9 + 3.3 * i);
        huge.stalls = {{1e9 + 6.6, 1e9 + 7.0, false}};
        huge.playback[2].firstFrameTime += 0.4;
        const PlaybackClock big(huge);
        QVERIFY(near(big.replayDuration(), 6.4, 1e-6));  // 1e9 + 0.4 is not exact
        QVERIFY(std::abs(big.replayToSim(3.0) - (1e9 + 4.95)) < 1e-6);
        QVERIFY(std::abs(big.simToReplay(1e9 + 4.95) - 3.0) < 1e-6);
        CHECK_RUN(huge);

        // A stall that lasts 1e9 s.
        RunData longStall = straightRun(2);
        longStall.stalls = {{7.6, 7.6 + 1e9, true}};
        QVERIFY(near(PlaybackClock(longStall).replayDuration(), 4.0 + 1e9));
        CHECK_RUN(longStall);
    }

    void nanAndInfiniteRunData()
    {
        // NaN times cannot be placed on the timeline; they must not poison the rest.
        RunData run = straightRun(3);
        run.playback.insert(run.playback.begin() + 1, {1, kNaN, kNaN, 100, 0});
        run.stalls = {{kNaN, 5.0, false}, {7.6, kNaN, false}, {kInf, kInf, false}, {-kInf, 2.0, false}};
        CHECK_RUN(run);

        RunData inf = straightRun(3);
        inf.playback[2].lastFrameTime = kInf;
        CHECK_RUN(inf);
        inf.playback[2].lastFrameTime = kNaN;
        CHECK_RUN(inf);
        inf.playback[1].firstFrameTime = -kInf;
        CHECK_RUN(inf);

        // Media durations that make no sense count as no media.
        for (double bad : {kNaN, kInf, -kInf, -2.0}) {
            RunData media = straightRun(3);
            media.segments[1].mediaDuration = bad;
            const PlaybackClock clock(media);
            QVERIFY2(clock.replayDuration() == 4.0, qPrintable(num(bad)));
            CHECK_RUN(media);
        }
        // So do stalls that end before they start.
        RunData backwards = straightRun(3);
        backwards.stalls = {{4.25, 3.0, false}};
        QCOMPARE(PlaybackClock(backwards).replayDuration(), 6.0);
        CHECK_RUN(backwards);
    }

    void nanAndInfiniteQueries()
    {
        RunData run = straightRun(4);
        run.stalls = {{7.6, 8.0, false}};
        run.playback[2].firstFrameTime = 8.0;
        run.playback[3].firstFrameTime = 11.3;
        const PlaybackClock clock(run);
        for (double t : {kNaN, -kNaN}) {
            for (double v : {clock.simToReplay(t), clock.replayToSim(t), clock.replayToMedia(t),
                             clock.mediaToReplay(t)})
                QVERIFY2(std::isfinite(v), "NaN in, NaN out");
            QVERIFY(std::isfinite(clock.positionAtSim(t).fraction));
            QVERIFY(std::isfinite(clock.positionAtReplay(t).fraction));
            QVERIFY(clock.positionAtReplay(t).beforeStart);
            QVERIFY(clock.positionAtSim(t).beforeStart);
        }
        QCOMPARE(clock.simToReplay(kInf), clock.replayDuration());
        QCOMPARE(clock.simToReplay(-kInf), 0.0);
        QCOMPARE(clock.replayToSim(kInf), clock.simEnd());
        QCOMPARE(clock.replayToSim(-kInf), clock.simStart());
        QVERIFY(clock.positionAtReplay(kInf).afterEnd);
        QVERIFY(clock.positionAtReplay(-kInf).beforeStart);
        QVERIFY(clock.positionAtSim(kInf).afterEnd);
    }

    void randomPlausibleRuns()
    {
        std::mt19937 rng(kSeed);
        for (int i = 0; i < 600; ++i) {
            const RunData run = randomValidRun(rng);
            const QString problem = allProblems(run, 400);
            QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("run %1: %2").arg(i).arg(problem)));

            // Total replay = played media + stalls after the first frame; the plan matches it.
            const PlaybackClock clock(run);
            double stalled = 0.0;
            for (const Stall& s : run.stalls) {
                if (!run.playback.empty() && s.start >= run.playback.front().firstFrameTime)
                    stalled += s.duration();
            }
            double media = 0.0;
            for (const SegmentPlayback& p : run.playback)
                media += run.segments[static_cast<std::size_t>(p.segment)].mediaDuration;
            if (run.playbackCutOff() && !run.playback.empty())
                media -= run.segments[static_cast<std::size_t>(run.playback.back().segment)].mediaDuration
                       * (1.0 - run.playback.back().framesPlayed / 100.0);
            QVERIFY2(near(clock.replayDuration(), media + stalled, 1e-9),
                     qPrintable(QStringLiteral("run %1").arg(i)));

            const QoeReport qoe = computeQoe(run);
            QCOMPARE(qoe.segmentsShown, static_cast<int>(run.playback.size()));
            QVERIFY(std::isfinite(qoe.averagePlayedBitrate) && std::isfinite(qoe.rebufferTime));
            QVERIFY(qoe.playedSwitches <= std::max(0, qoe.segmentsShown - 1));
        }
    }

    void randomNastyRuns()
    {
        std::mt19937 rng(kSeed + 1);
        for (int i = 0; i < 800; ++i) {
            const RunData run = randomNastyRun(rng);
            const QString problem = allProblems(run, 200);
            QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("run %1: %2").arg(i).arg(problem)));
            const QoeReport qoe = computeQoe(run);
            QVERIFY(std::isfinite(qoe.averagePlayedBitrate));
            QVERIFY(makeRenderPlan(run).cacheKey(QStringLiteral("r")).startsWith(QStringLiteral("r-")));
        }
    }

    void realBundlesSweepWithoutGoingBackwards()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ is not present");
        const QDir root(test::bundlesPath());
        int checked = 0;
        for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto result = loadBundleSync(root.filePath(name));
            if (!result.run)
                continue;
            const PlaybackClock clock(*result.run);
            // 5 ms steps over the whole replay.
            const int steps = static_cast<int>(clock.replayDuration() / 0.005);
            for (const QString& problem : {pieceProblem(clock), sweepProblem(clock, steps),
                                           boundaryProblem(clock), planProblem(*result.run, clock)})
                QVERIFY2(problem.isEmpty(), qPrintable(name + QStringLiteral(": ") + problem));
            ++checked;
        }
        QVERIFY(checked > 0);
    }

    void qoeWithoutStallsOrSwitches()
    {
        RunData run = straightRun(4);
        run.streamStart = 0.5;
        run.playbackStart = 1.0;
        run.decisions = {{.record = 0, .forSegment = 1, .chosenRung = 0},
                         {.record = 1, .forSegment = 2, .chosenRung = 0}};
        const QoeReport qoe = computeQoe(run);
        QCOMPARE(qoe.stallCount, 0);
        QCOMPARE(qoe.rebufferTime, 0.0);
        QCOMPARE(qoe.playedSwitches, 0);
        QCOMPARE(qoe.chosenSwitches, 0);
        QCOMPARE(qoe.averagePlayedBitrate, 1e6);
        QCOMPARE(qoe.averageChosenBitrate, 1e6);
        QCOMPARE(qoe.averageDecisionDelay, 0.0);  // no delays recorded
        QCOMPARE(qoe.maxDecisionDelay, 0.0);
        QCOMPARE(qoe.startupDelay, 0.5);
        QVERIFY(!qoe.cutOff);
        QVERIFY(qoe.agreesWithSummary());
    }

    void qoeSwitchingEverySegment()
    {
        RunData run = straightRun(6);
        run.video.ladder = {{0, 250'000, 428, 240}, {1, 750'000, 640, 360}};
        for (Segment& s : run.segments)
            s.playedRung = s.index % 2;
        for (int i = 0; i < 6; ++i)
            run.decisions.push_back({.record = i, .forSegment = i + 1, .chosenRung = (i + 1) % 2});
        run.decisions.push_back({.record = 6, .forSegment = 7, .chosenRung = 5});  // no such rung
        run.decisions.push_back({.record = 7, .forSegment = 8, .chosenRung = -1});
        const QoeReport qoe = computeQoe(run);
        QCOMPARE(qoe.segmentsShown, 6);
        QCOMPARE(qoe.playedSwitches, 5);
        QCOMPARE(qoe.chosenSwitches, 5);  // unknown rungs are skipped, not counted as changes
        QCOMPARE(qoe.averagePlayedBitrate, 500'000.0);
        QCOMPARE(qoe.averageChosenBitrate, 500'000.0);
        QCOMPARE(qoe.decisions, 8);

        // Played rungs outside the ladder are not shown.
        run.segments[0].playedRung = 9;
        QCOMPARE(computeQoe(run).segmentsShown, 5);
    }

    void qoeOfDegenerateRuns()
    {
        // Nothing at all.
        const RunData nothing;
        const QoeReport empty = computeQoe(nothing);
        QCOMPARE(empty.segmentsTotal, 0);
        QCOMPARE(empty.segmentsShown, 0);
        QCOMPARE(empty.lastSegmentShown, -1);
        QCOMPARE(empty.lastSegmentFrames, 0);
        for (double v : {empty.averagePlayedBitrate, empty.averageChosenBitrate, empty.startupDelay,
                         empty.rebufferTime, empty.averageThroughput, empty.averageDecisionDelay,
                         empty.maxDecisionDelay})
            QCOMPARE(v, 0.0);
        QVERIFY(!empty.cutOff);
        QVERIFY(empty.agreesWithSummary());

        // Only stalls, no playback.
        RunData stalls = downloadedRun(2);
        stalls.stalls = {{1.0, 2.0, false}, {3.0, 3.0, false}, {4.0, 10.0, true}};
        const QoeReport s = computeQoe(stalls);
        QCOMPARE(s.stallCount, 3);
        QCOMPARE(s.rebufferTime, 7.0);
        QCOMPARE(s.segmentsShown, 0);

        // A one-rung ladder and a single segment.
        RunData one = straightRun(1);
        one.decisions = {{.record = 0, .forSegment = 1, .chosenRung = 0, .decisionDelay = 0.0, .used = false}};
        const QoeReport single = computeQoe(one);
        QCOMPARE(single.playedSwitches, 0);
        QCOMPARE(single.chosenSwitches, 0);
        QCOMPARE(single.decisions, 0);  // unused decisions are not counted
        QCOMPARE(single.averageChosenBitrate, 1e6);  // but their choice is, as summary.json does

        // Summary values that are zero, and values that disagree only near zero.
        one.summary.rebufferTime = 0.0;
        one.summary.stallCount = 0;
        one.summary.startupDelay = 0.0005;
        QVERIFY(computeQoe(one).agreesWithSummary());
        one.summary.startupDelay = 0.01;
        QVERIFY(!computeQoe(one).agreesWithSummary());

        // A NaN in summary.json never agrees (and does not crash).
        SummaryCheck nan{QStringLiteral("x"), 1.0, kNaN};
        QVERIFY(!nan.agrees());
        SummaryCheck nanComputed{QStringLiteral("x"), kNaN, 1.0};
        QVERIFY(!nanComputed.agrees());
    }

    void renderPlanOfDegenerateRuns()
    {
        // Frame rates that are unknown or nonsense fall back to 30.
        for (double rate : {0.0, -25.0, kNaN, kInf}) {
            RunData run = straightRun(2);
            run.video.frameRate = rate;
            const RenderPlan plan = makeRenderPlan(run);
            QVERIFY2(plan.frameRate == 30.0, qPrintable(num(rate)));
            // The arguments are built without undefined behaviour (checked under UBSan).
            const QStringList args = transcodeArguments(plan, plan.parts.front(), QStringLiteral("/o.mp4"));
            QVERIFY(args.contains(QStringLiteral("-g")));
        }
        // Huge frame rates do not overflow the keyframe interval.
        RenderPlan fast;
        fast.frameRate = 1e300;
        const QStringList args = transcodeArguments(fast, {0, QStringLiteral("/m.mp4"), 1.0, 0.0},
                                                    QStringLiteral("/o.mp4"));
        QVERIFY(args[args.indexOf(QStringLiteral("-g")) + 1].toInt() >= 1);

        // A zero-length part has no freeze filter and a zero trim.
        const RenderPart zero{0, QStringLiteral("/m.mp4"), 0.0, 0.0};
        const QStringList zeroArgs = transcodeArguments(RenderPlan{}, zero, QStringLiteral("/o.mp4"));
        const QString filters = zeroArgs[zeroArgs.indexOf(QStringLiteral("-vf")) + 1];
        QVERIFY(filters.startsWith(QStringLiteral("trim=duration=0.000000")));
        QVERIFY(!filters.contains(QStringLiteral("tpad")));

        // A record for a segment that was not downloaded: no part, its stall freezes the part
        // before it, and the plan still matches the clock.
        RunData missing = straightRun(3);
        missing.segments.erase(missing.segments.begin() + 1);
        missing.stalls = {{7.6, 8.0, false}};
        missing.playback[2].firstFrameTime = 8.0;
        const RenderPlan plan = makeRenderPlan(missing);
        QCOMPARE(plan.parts.size(), 2u);
        QVERIFY(near(plan.parts[0].freezeAfter, 0.4));
        CHECK_RUN(missing);
    }

    void cacheKeyChangesWithEveryInput()
    {
        RunData run = straightRun(3);
        run.stalls = {{7.6, 8.0, false}};
        run.playback[2].firstFrameTime = 8.0;
        const RenderPlan base = makeRenderPlan(run);
        const QString id = QStringLiteral("run");
        const QString key = base.cacheKey(id);
        QCOMPARE(makeRenderPlan(run).cacheKey(id), key);  // stable
        QVERIFY(key.startsWith(QStringLiteral("run-")));
        QCOMPARE(key.size(), qsizetype{4 + 12});

        QStringList keys{key};
        auto vary = [&](auto change) {
            RenderPlan plan = base;
            change(plan);
            keys << plan.cacheKey(id);
        };
        vary([](RenderPlan& p) { p.width = 1280; });
        vary([](RenderPlan& p) { p.height = 720; });
        vary([](RenderPlan& p) { p.frameRate = 25.0; });
        vary([](RenderPlan& p) { p.frameRate = 30.000001; });
        vary([](RenderPlan& p) { p.parts[0].segment = 7; });
        vary([](RenderPlan& p) { p.parts[0].mediaPath += QLatin1Char('x'); });
        vary([](RenderPlan& p) { p.parts[0].duration += 1e-6; });
        vary([](RenderPlan& p) { p.parts[1].freezeAfter = 0.0; });
        vary([](RenderPlan& p) { std::swap(p.parts[0], p.parts[1]); });
        vary([](RenderPlan& p) { p.parts.pop_back(); });
        vary([](RenderPlan& p) { p.parts.push_back({}); });
        vary([](RenderPlan& p) { p.parts.clear(); });
        // A freeze moved from one part to the next.
        vary([](RenderPlan& p) {
            p.parts[2].freezeAfter = p.parts[1].freezeAfter;
            p.parts[1].freezeAfter = 0.0;
        });
        keys << base.cacheKey(QStringLiteral("run2"));
        keys << base.cacheKey(QString());
        keys.removeDuplicates();
        QCOMPARE(keys.size(), qsizetype{16});

        // Below the precision of the ffmpeg arguments the output is the same, and so is the key.
        RenderPlan close = base;
        close.parts[0].duration += 1e-8;
        QCOMPARE(close.cacheKey(id), key);

        // Paths may contain the separator; parts must not run into each other.
        RenderPlan two;
        two.parts = {{1, QStringLiteral("p"), 1.0, 0.0}, {2, QStringLiteral("q"), 1.0, 0.0}};
        RenderPlan one;
        one.parts = {{1, QStringLiteral("p|1.000000|0.000000|2|q"), 1.0, 0.0}};
        QVERIFY2(one.cacheKey(id) != two.cacheKey(id), "two plans share a cache key");

        // An empty plan has a key too, and it is stable.
        QCOMPARE(RenderPlan{}.cacheKey(id), RenderPlan{}.cacheKey(id));
    }

    void runDataLookupsAtTheEdges()
    {
        RunData run = straightRun(3);
        QVERIFY(!run.playbackAt(0.0));
        QCOMPARE(run.playbackAt(1.0)->segment, 0);
        QCOMPARE(run.playbackAt(std::nextafter(4.3, 0.0))->segment, 0);
        QCOMPARE(run.playbackAt(1e9)->segment, 2);
        QVERIFY(!run.playbackAt(-kInf));
        run.samples = {{.time = 1.0}, {.time = 2.0}, {.time = 3.0}};
        QCOMPARE(run.samplesBetween(1.0, 3.0).size(), 3u);
        QCOMPARE(run.samplesBetween(3.0, 1.0).size(), 0u);  // reversed range
        QCOMPARE(run.samplesBetween(2.0, 2.0).size(), 1u);
        QCOMPARE(run.samplesBetween(kNaN, kNaN).size(), 0u);
        QCOMPARE(run.bufferBetween(0.0, 10.0).size(), 0u);
        QVERIFY(!RunData{}.playbackCutOff());
    }
};

QTEST_GUILESS_MAIN(TestPlaybackEdgeCases)
#include "tst_playback_edge_cases.moc"
