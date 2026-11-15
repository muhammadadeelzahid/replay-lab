#include "background_mode.h"
#include "synthetic_run.h"
#include "test_paths.h"

#include "abrviz/core/render/render_queue.h"
#include "abrviz/media/prerendered_source.h"

#if ABRVIZ_WITH_GSTREAMER
#include "gst/gst_segment_source.h"
#endif

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPointer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace abrviz;
using State = ReplaySource::State;

// On macOS, Qt's AVFoundation backend delivers media through the Cocoa run loop, which the
// offscreen platform does not run. CTest runs this binary twice there (see tests/CMakeLists.txt):
// offscreen for the GStreamer engine and with the Cocoa platform for the pre-rendered engine;
// each pass skips the other's cases. Elsewhere one offscreen pass runs everything.
#define REQUIRE_GSTREAMER_PASS()                                                               \
    do {                                                                                       \
        if (cocoaPass())                                                                       \
            QSKIP("the GStreamer engine is tested in the offscreen pass");                    \
    } while (false)

#define REQUIRE_PRERENDERED_PASS()                                                             \
    do {                                                                                       \
        if (!prerenderedPass())                                                                \
            QSKIP("the pre-rendered engine is tested with the Cocoa platform");               \
        if (!queue_)                                                                           \
            QSKIP("ffmpeg not found");                                                         \
    } while (false)

namespace {

// Generous: under ThreadSanitizer everything runs about ten times slower.
constexpr int kTimeoutMs = 30000;
[[maybe_unused]] constexpr double kFrame = 1.0 / 30.0;  // the synthetic run's frame duration
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

[[nodiscard]] bool cocoaPass()
{
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

[[nodiscard]] bool prerenderedPass()
{
#ifdef Q_OS_MACOS
    return cocoaPass();
#else
    return true;
#endif
}

/// Spins the event loop until `done()` holds or `timeoutMs` passes.
template <typename Predicate>
bool waitUntil(Predicate done, int timeoutMs = kTimeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return done();
        QTest::qWait(5);
    }
    return true;
}

bool waitForState(const ReplaySource& source, State state, int timeoutMs = kTimeoutMs)
{
    return waitUntil([&] { return source.state() == state; }, timeoutMs);
}

/// The synthetic run (see synthetic_run.h) with every segment's media replaced by `media`.
std::shared_ptr<const RunData> runWithMedia(const QString& media, const QString& id, int onlySegment = -1)
{
    auto run = std::make_shared<RunData>(*test::syntheticRun(1.0, id));
    for (Segment& segment : run->segments) {
        if (onlySegment < 0 || segment.index == onlySegment)
            segment.mediaPath = media;
    }
    return run;
}

std::shared_ptr<const RunData> corruptRun(int onlySegment = -1)
{
    return runWithMedia(test::dataPath(QStringLiteral("media/not_an_mp4.mp4")),
                        QStringLiteral("corrupt%1").arg(onlySegment), onlySegment);
}

/// The synthetic run with a `stall` s stall before the first frame (and the usual 1 s stall
/// after the second segment). Replay time: stall [0, stall), segments [stall, stall + 5),
/// stall [stall + 5, stall + 6), the cut-off third segment [stall + 6, stall + 7.25).
[[maybe_unused]] std::shared_ptr<const RunData> runWithStallAtStart(double stall)
{
    auto run = std::make_shared<RunData>(*test::syntheticRun(1.0, QStringLiteral("stall-at-start")));
    for (SegmentPlayback& p : run->playback) {
        p.firstFrameTime += stall;
        p.lastFrameTime += stall;
    }
    for (Stall& s : run->stalls) {
        s.start += stall;
        s.end += stall;
    }
    run->stalls.insert(run->stalls.begin(), Stall{run->playbackStart, run->playbackStart + stall, false});
    run->playbackEnd += stall;
    return run;
}

/// Three full segments, the usual 1 s stall after the second, and a terminal 1 s stall after
/// the third: the replay ends inside a stall. Replay time: segments [0, 5), stall [5, 6),
/// segment [6, 8.5) (media 5 to 7.5), terminal stall [8.5, 9.5).
[[maybe_unused]] std::shared_ptr<const RunData> runEndingInAStall()
{
    auto run = std::make_shared<RunData>(*test::syntheticRun(1.0, QStringLiteral("ends-stalled")));
    run->playback[2].lastFrameTime = run->playback[2].firstFrameTime + 2.467;
    run->playback[2].framesPlayed = 100;
    const double end = run->playback[2].firstFrameTime + 2.5;
    run->stalls.push_back({end, end + 1.0, true});
    run->playbackEnd = end + 1.0;
    return run;
}

/// The media time shown at `replay` in the synthetic run with a stall of `stall` s: during the
/// stall, the stall's media time (5.0; the frame on screen is the one just before it).
[[maybe_unused]] double expectedMedia(double replay, double stall)
{
    if (replay < 5.0)
        return std::max(replay, 0.0);
    if (replay < 5.0 + stall)
        return 5.0;
    return std::min(replay - stall, 6.25);
}

/// The picture in `sink`, small and grey, for comparing engines.
[[maybe_unused]] QImage thumbnail(const QVideoSink& sink)
{
    return sink.videoFrame().toImage().scaled(96, 54, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

/// Mean absolute difference of two thumbnails, 0..255.
[[maybe_unused]] double difference(const QImage& a, const QImage& b)
{
    if (a.size() != b.size() || a.isNull())
        return 255.0;
    double total = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* ra = a.constScanLine(y);
        const uchar* rb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x)
            total += std::abs(int(ra[x]) - int(rb[x]));
    }
    return total / (a.width() * a.height());
}

/// A sequence of pseudo-random replay times over [lo, hi], the same on every run.
std::vector<double> stormTargets(int count, double lo, double hi, quint32 seed)
{
    QRandomGenerator random(seed);
    std::vector<double> targets;
    for (int i = 0; i < count; ++i)
        targets.push_back(lo + random.generateDouble() * (hi - lo));
    return targets;
}

#if ABRVIZ_WITH_GSTREAMER

/// What a listener saw when a signal arrived.
struct Report {
    double position;
    State state;
    double media;
};

/// Records every `positionChanged`, and every frame handed to the sink, with the engine's state
/// and frame media time at that moment.
class Recorder {
public:
    Recorder(const GstSegmentSource& source, const QVideoSink& sink)
    {
        positionConnection_ = QObject::connect(&source, &ReplaySource::positionChanged, &source,
                                               [this, &source](double position) {
                                                   positions.push_back({position, source.state(), source.frameMediaTime()});
                                               });
        frameConnection_ = QObject::connect(&sink, &QVideoSink::videoFrameChanged, &sink, [this, &source] {
            frames.push_back({source.position(), source.state(), source.frameMediaTime()});
        });
    }
    ~Recorder()
    {
        QObject::disconnect(positionConnection_);
        QObject::disconnect(frameConnection_);
    }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    std::vector<Report> positions;
    std::vector<Report> frames;

private:
    QMetaObject::Connection positionConnection_;
    QMetaObject::Connection frameConnection_;
};

/// Checks reports made while playing through the synthetic run's stall [5, 5 + stall) without
/// seeking: positions never go back, the state matches the position, a stalled report always
/// carries the frame held from before the stall, and a report after the stall never carries a
/// frame from before it or one ahead of the position. Returns an empty string or the failure.
QString checkPlaythrough(const std::vector<Report>& positions, const std::vector<Report>& frames, double stall)
{
    constexpr double eps = 1e-6;
    const double stallStart = 5.0;
    const double stallEnd = 5.0 + stall;
    double last = -1.0;
    for (const Report& s : positions) {
        const QString where = QStringLiteral("position %1, state %2, media %3")
                                  .arg(s.position).arg(int(s.state)).arg(s.media);
        if (s.position < last - eps)
            return QStringLiteral("went back from %1: %2").arg(last).arg(where);
        last = s.position;
        if (s.state == State::Stalled) {
            if (s.position < stallStart - eps || s.position > stallEnd + eps)
                return QStringLiteral("stalled outside the stall: ") + where;
            if (s.media < 5.0 - 0.1 || s.media >= 5.0 - 1e-4)
                return QStringLiteral("stalled without the held frame: ") + where;
        } else if (s.state == State::Playing) {
            if (s.position > stallStart + eps && s.position < stallEnd - eps)
                return QStringLiteral("playing inside the stall: ") + where;
            if (s.media > expectedMedia(s.position, stall) + 0.05)
                return QStringLiteral("frame ahead of the position: ") + where;
            if (s.position >= stallEnd - eps && s.media < 5.0 - 0.1)
                return QStringLiteral("frame from before the stall after it: ") + where;
        }
    }
    for (const Report& s : frames) {
        if (s.state == State::Stalled && s.media >= 5.0 - 1e-4)
            return QStringLiteral("a frame from after the stall was shown while stalled: media %1").arg(s.media);
    }
    return {};
}

#endif // ABRVIZ_WITH_GSTREAMER

} // namespace

class TestEngineEdgeCases : public QObject {
    Q_OBJECT

private:
    QTemporaryDir cacheDir_;
    std::unique_ptr<RenderQueue> queue_;

    /// A pre-rendered source for `run`, rendered (or taken from the cache) and paused.
    std::unique_ptr<PrerenderedSource> rendered(QVideoSink* sink, std::shared_ptr<const RunData> run)
    {
        auto source = std::make_unique<PrerenderedSource>(queue_.get());
        source->setVideoSink(sink);
        source->setRun(std::move(run));
        source->prepare();
        if (!waitForState(*source, State::Paused, 60000))
            return nullptr;
        return source;
    }

#if ABRVIZ_WITH_GSTREAMER
    /// A GStreamer source for `run`, prerolled, paused and showing its first frame.
    static std::unique_ptr<GstSegmentSource> ready(QVideoSink* sink, std::shared_ptr<const RunData> run)
    {
        auto source = std::make_unique<GstSegmentSource>();
        source->setVideoSink(sink);
        source->setRun(std::move(run));
        if (!waitForState(*source, State::Paused) || !waitUntil([&] { return source->framesShown() > 0; }))
            return nullptr;
        return source;
    }

    static std::unique_ptr<GstSegmentSource> ready(QVideoSink* sink, double stall = 1.0)
    {
        return ready(sink, test::syntheticRun(stall));
    }

    /// Seeks and waits for the first frame after the seek.
    static bool seekAndShow(GstSegmentSource& source, double replay)
    {
        const qint64 before = source.framesShown();
        source.seek(replay);
        return waitUntil([&] { return source.framesShown() > before; });
    }
#endif

private slots:
    void initTestCase()
    {
        if (cocoaPass())
            abrviz::runInBackground();  // never take the focus from the app in use
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (!ffmpeg.isEmpty())
            queue_ = std::make_unique<RenderQueue>(RenderQueue::Options{cacheDir_.path(), ffmpeg, 0});
    }

    void cleanupTestCase() { queue_.reset(); }

#if ABRVIZ_WITH_GSTREAMER
    // ---- GStreamer engine: seeking -------------------------------------------------------

    void gstSeekToEdgeTimes()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        // Replay: segments [0, 5), stall [5, 6), cut-off segment [6, 7.25); media 0..6.25.
        // The frame's media time: an accurate seek reports the target for the frame that covers
        // it; in the stall, the frame held from just before it.
        constexpr double held[] = {5.0 - kFrame - 0.005, 5.0};
        constexpr double last[] = {6.25 - kFrame - 0.005, 6.25};
        struct Case {
            double target;
            double position;
            double mediaLo;
            double mediaHi;
        };
        const Case cases[] = {
            {-5.0, 0.0, 0.0, 0.01},  {-0.0, 0.0, 0.0, 0.01},  {0.0, 0.0, 0.0, 0.01},
            {-kInf, 0.0, 0.0, 0.01}, {4.98, 4.98, 4.96, 4.99},
            {5.0, 5.0, held[0], held[1]},  // the stall's first instant
            {5.0 + 1e-9, 5.0, held[0], held[1]},
            {6.0 - 1e-3, 6.0 - 1e-3, held[0], held[1]},
            {6.0, 6.0, 5.0 - 0.001, 5.01},  // the stall's end: the first frame after it
            {7.25, 7.25, last[0], last[1]},  // the end: the last frame played
            {7.25 + 1e-9, 7.25, last[0], last[1]},
            {100.0, 7.25, last[0], last[1]},
            {kInf, 7.25, last[0], last[1]},
        };
        for (const Case& c : cases) {
            // From a distinct picture each time, so a seek that shows nothing is noticed.
            QVERIFY(seekAndShow(*source, 2.0));
            QSignalSpy frames(&sink, &QVideoSink::videoFrameChanged);
            const qint64 before = source->framesShown();
            source->seek(c.target);
            const QString what = QStringLiteral("seek %1").arg(c.target);
            QVERIFY2(std::abs(source->position() - c.position) < 1e-6,
                     qPrintable(what + QStringLiteral(": position %1").arg(source->position())));
            QCOMPARE(source->state(), State::Paused);
            QVERIFY2(waitUntil([&] { return source->framesShown() > before; }),
                     qPrintable(what + QStringLiteral(": no frame")));
            QVERIFY(!frames.isEmpty());
            QVERIFY2(source->frameMediaTime() >= c.mediaLo && source->frameMediaTime() < c.mediaHi,
                     qPrintable(what + QStringLiteral(": media %1, expected [%2, %3)")
                                           .arg(source->frameMediaTime()).arg(c.mediaLo).arg(c.mediaHi)));
            QCOMPARE(source->state(), State::Paused);
        }
    }

    void gstSeekToNaNIsIgnored()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, 2.0));
        QSignalSpy positions(source.get(), &ReplaySource::positionChanged);
        source->seek(kNaN);
        QCOMPARE(source->position(), 2.0);
        QVERIFY(positions.isEmpty());
        QCOMPARE(source->state(), State::Paused);
        QTest::qWait(200);
        QVERIFY(std::abs(source->frameMediaTime() - 2.0) < 0.03);

        // While playing, too: the clock runs on from where it was.
        source->play();
        source->seek(kNaN);
        QVERIFY(waitUntil([&] { return source->position() > 2.2; }));
        QVERIFY(std::isfinite(source->position()));
        QCOMPARE(source->state(), State::Playing);
        source->pause();

        // Before the preroll: a NaN seek is not kept; the engine starts at 0.
        auto fresh = std::make_unique<GstSegmentSource>();
        fresh->setVideoSink(&sink);
        fresh->setRun(test::syntheticRun());
        fresh->seek(kNaN);
        QVERIFY(waitForState(*fresh, State::Paused));
        QVERIFY(waitUntil([&] { return fresh->framesShown() > 0; }));
        QCOMPARE(fresh->position(), 0.0);
        QVERIFY(std::abs(fresh->frameMediaTime()) < 0.03);
    }

    void gstSeekStormWhilePaused()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        // Final targets in a segment, in the stall and at the end; the storm before each avoids
        // the second around the final target, so a stale frame from the storm is told apart.
        for (const double final : {2.0, 5.5, 7.25}) {
            int n = 0;
            for (const double t : stormTargets(300, 0.0, 7.25, quint32(final * 100))) {
                if (std::abs(expectedMedia(t, 1.0) - expectedMedia(final, 1.0)) < 1.2)
                    continue;
                source->seek(t);
                if (++n % 7 == 0)
                    QCoreApplication::processEvents();  // let some of the seeks' frames arrive
            }
            QVERIFY(n > 100);
            const qint64 before = source->framesShown();
            source->seek(final);
            QCOMPARE(source->position(), final);
            // The last frame played, the frame held in the stall, the frame at the target.
            const double media = final == 7.25 ? 6.25 - kFrame : final == 5.5 ? 5.0 - kFrame / 2.0 : final;
            QVERIFY2(waitUntil([&] {
                         return source->framesShown() > before && std::abs(source->frameMediaTime() - media) < 0.03;
                     }),
                     qPrintable(QStringLiteral("final %1: media %2").arg(final).arg(source->frameMediaTime())));
            // No late frame from the storm replaces it, and the position stays.
            QTest::qWait(300);
            QVERIFY2(std::abs(source->frameMediaTime() - media) < 0.03,
                     qPrintable(QStringLiteral("final %1: media %2 later").arg(final).arg(source->frameMediaTime())));
            QCOMPARE(source->position(), final);
            QCOMPARE(source->state(), State::Paused);
        }
    }

    void gstSeekStormWhilePlaying()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        source->play();
        int n = 0;
        // Targets away from the final one (1.0, media 1.0): at least 1.5 s of media from it.
        for (const double t : stormTargets(400, 2.5, 7.25, 7)) {
            source->seek(t);
            QVERIFY(source->state() == State::Playing || source->state() == State::Stalled);
            if (++n % 5 == 0)
                QCoreApplication::processEvents();
        }
        Recorder recorder(*source, sink);
        source->seek(1.0);
        QCOMPARE(source->position(), 1.0);
        QCOMPARE(source->state(), State::Playing);
        QVERIFY(waitUntil([&] { return source->position() > 1.4; }));
        source->pause();
        QCOMPARE(source->state(), State::Paused);
        // Everything after the final seek is from it: no frame from the storm, positions only
        // forward from the target, frames never ahead of the position.
        QVERIFY(!recorder.positions.empty());
        double last = 1.0;
        for (const Report& s : recorder.positions) {
            QVERIFY2(s.position >= last - 1e-6, qPrintable(QStringLiteral("%1 after %2").arg(s.position).arg(last)));
            last = s.position;
            QVERIFY2(s.media > 1.0 - 0.05 && s.media < s.position + 0.05,
                     qPrintable(QStringLiteral("position %1: media %2").arg(s.position).arg(s.media)));
        }
        for (const Report& s : recorder.frames)
            QVERIFY2(s.media > 1.0 - 0.05 && s.media < 2.0, qPrintable(QString::number(s.media)));
    }

    // ---- GStreamer engine: play and pause ------------------------------------------------

    void gstPlayPauseSpam()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, 3.0));
        Recorder recorder(*source, sink);
        for (int i = 0; i < 400; ++i) {
            source->play();
            source->play();
            if (i % 3 == 0)
                QCoreApplication::processEvents();
            source->pause();
            source->pause();
            if (i % 5 == 0)
                QCoreApplication::processEvents();
        }
        QCOMPARE(source->state(), State::Paused);
        const double paused = source->position();
        QVERIFY2(paused >= 3.0 && paused < 7.25, qPrintable(QString::number(paused)));
        QTest::qWait(200);
        QCOMPARE(source->position(), paused);  // a paused clock does not run on
        // Positions never went back, and the engine still plays.
        double last = 3.0;
        for (const Report& s : recorder.positions) {
            QVERIFY2(s.position >= last - 1e-6, qPrintable(QStringLiteral("%1 after %2").arg(s.position).arg(last)));
            last = s.position;
        }
        for (int i = 0; i < 50; ++i) {
            source->pause();
            source->play();
        }
        QVERIFY(source->state() == State::Playing || source->state() == State::Stalled);
        QVERIFY(waitUntil([&] { return source->position() > paused + 0.3; }));
    }

    void gstPlayWhenEndedRestarts()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, 6.9));
        source->play();
        QVERIFY(waitForState(*source, State::Ended));
        QCOMPARE(source->position(), 7.25);
        QVERIFY2(std::abs(source->frameMediaTime() - (6.25 - kFrame)) < 0.05, qPrintable(QString::number(source->frameMediaTime())));
        // Ended stays ended: no further reports.
        QSignalSpy positions(source.get(), &ReplaySource::positionChanged);
        QTest::qWait(200);
        QVERIFY(positions.isEmpty());
        QCOMPARE(source->state(), State::Ended);

        // Pausing an ended engine changes nothing; playing starts again from 0.
        source->pause();
        QCOMPARE(source->state(), State::Ended);
        source->play();
        QVERIFY(!positions.isEmpty());
        QCOMPARE(positions.first().first().toDouble(), 0.0);
        QCOMPARE(source->state(), State::Playing);
        source->play();  // again: already playing
        QVERIFY(waitUntil([&] { return source->position() > 0.3; }));
        QVERIFY(source->position() < 3.0);
        QVERIFY2(source->frameMediaTime() < 3.0, qPrintable(QString::number(source->frameMediaTime())));

        // Paused exactly at the end, play starts again from 0 too.
        source->pause();
        QVERIFY(seekAndShow(*source, 7.25));
        QCOMPARE(source->state(), State::Paused);
        source->play();
        QCOMPARE(source->state(), State::Playing);
        QVERIFY(source->position() < 0.1);

        // A seek after the end moves back to Paused.
        QVERIFY(waitUntil([&] { return source->position() > 0.2; }));
        source->seek(7.0);
        QVERIFY(waitForState(*source, State::Ended));
        QVERIFY(seekAndShow(*source, 3.0));
        QCOMPARE(source->state(), State::Paused);
        QCOMPARE(source->position(), 3.0);
    }

    void gstPauseAtStallBoundaries()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);

        // Paused exactly at the stall's start: play stalls at once, pause holds the frame.
        QVERIFY(seekAndShow(*source, 5.0));
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        source->pause();
        QCOMPARE(source->state(), State::Paused);
        QVERIFY(source->position() >= 5.0 && source->position() < 5.5);
        QVERIFY(source->frameMediaTime() < 5.0);

        // Exactly at the stall's end: play plays.
        QVERIFY(seekAndShow(*source, 6.0));
        source->play();
        QCOMPARE(source->state(), State::Playing);
        source->pause();
        QVERIFY(source->position() >= 6.0);

        // Pausing from the listener of the change into the stall (re-entrant, inside a tick).
        QVERIFY(seekAndShow(*source, 4.7));
        Recorder recorder(*source, sink);
        auto pauseOn = QObject::connect(source.get(), &ReplaySource::stateChanged, source.get(),
                                        [&](State state) {
                                            if (state == State::Stalled)
                                                source->pause();
                                        });
        source->play();
        QVERIFY(waitForState(*source, State::Paused));
        QObject::disconnect(pauseOn);
        const double at = source->position();
        QVERIFY2(at >= 5.0 - 1e-6 && at < 6.0, qPrintable(QString::number(at)));
        QTest::qWait(300);
        QCOMPARE(source->state(), State::Paused);
        QCOMPARE(source->position(), at);
        QVERIFY2(source->frameMediaTime() > 4.9 && source->frameMediaTime() < 5.0,
                 qPrintable(QString::number(source->frameMediaTime())));
        const QString failure = checkPlaythrough(recorder.positions, recorder.frames, 1.0);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));

        // Resuming there stalls for the rest of the stall, then plays.
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        QVERIFY(waitForState(*source, State::Playing));
        QVERIFY(source->position() >= 6.0 - 1e-6);

        // Pausing from the listener of the change out of the stall.
        QVERIFY(seekAndShow(*source, 5.7));
        pauseOn = QObject::connect(source.get(), &ReplaySource::stateChanged, source.get(),
                                   [&](State state) {
                                       if (state == State::Playing)
                                           source->pause();
                                   });
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        QVERIFY(waitForState(*source, State::Paused));
        QObject::disconnect(pauseOn);
        QVERIFY2(source->position() >= 6.0 - 1e-6 && source->position() < 7.0,
                 qPrintable(QString::number(source->position())));
        QVERIFY2(source->frameMediaTime() > 4.9, qPrintable(QString::number(source->frameMediaTime())));
    }

    void gstPauseInsideStallThenSeekOut()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, 5.3));
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        QTest::qWait(150);
        source->pause();
        QCOMPARE(source->state(), State::Paused);
        QVERIFY(source->position() > 5.3 && source->position() < 6.0);

        // Backwards out of it, then forwards past it, then play: no stall left over.
        QVERIFY(seekAndShow(*source, 2.0));
        QCOMPARE(source->state(), State::Paused);
        QVERIFY(std::abs(source->frameMediaTime() - 2.0) < 0.03);
        QVERIFY(seekAndShow(*source, 6.5));
        QVERIFY(std::abs(source->frameMediaTime() - 5.5) < 0.03);
        Recorder recorder(*source, sink);
        source->play();
        QCOMPARE(source->state(), State::Playing);
        QVERIFY(waitForState(*source, State::Ended));
        for (const Report& s : recorder.positions)
            QVERIFY(s.state != State::Stalled);
        QVERIFY(std::abs(source->frameMediaTime() - (6.25 - kFrame)) < 0.05);
    }

    // ---- GStreamer engine: unusual timelines ---------------------------------------------

    void gstZeroLengthStall()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink, 0.0);
        QVERIFY(source);
        // The stall takes no replay time: the third segment follows the second at once.
        QVERIFY(seekAndShow(*source, 5.0));
        QVERIFY(std::abs(source->frameMediaTime() - 5.0) < 0.03);
        QVERIFY(seekAndShow(*source, 4.6));
        Recorder recorder(*source, sink);
        source->play();
        QVERIFY(waitForState(*source, State::Ended));
        QCOMPARE(source->position(), 6.25);
        for (const Report& s : recorder.positions)
            QVERIFY2(s.state != State::Stalled, qPrintable(QString::number(s.position)));
        const QString failure = checkPlaythrough(recorder.positions, recorder.frames, 0.0);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(std::abs(source->frameMediaTime() - (6.25 - kFrame)) < 0.05);
    }

    void gstVeryLongStall()
    {
        REQUIRE_GSTREAMER_PASS();
        constexpr double stall = 3600.0;  // an hour
        QVideoSink sink;
        auto source = ready(&sink, stall);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, 1800.0));
        QCOMPARE(source->position(), 1800.0);
        const auto isHeld = [](double media) { return media > 5.0 - kFrame - 0.005 && media < 5.0; };
        QVERIFY2(isHeld(source->frameMediaTime()), qPrintable(QString::number(source->frameMediaTime())));

        Recorder recorder(*source, sink);
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        QVERIFY(waitUntil([&] { return source->position() > 1800.3; }));
        QCOMPARE(source->state(), State::Stalled);
        QVERIFY2(isHeld(source->frameMediaTime()), qPrintable(QString::number(source->frameMediaTime())));
        // Near the stall's end: the clock runs out of it and on to the end.
        source->seek(5.0 + stall - 0.2);
        QCOMPARE(source->state(), State::Stalled);
        QVERIFY(waitForState(*source, State::Playing));
        QVERIFY(waitForState(*source, State::Ended));
        QCOMPARE(source->position(), 7.25 + stall - 1.0);
        QVERIFY(std::abs(source->frameMediaTime() - (6.25 - kFrame)) < 0.05);
        for (const Report& s : recorder.positions) {
            if (s.state == State::Stalled)
                QVERIFY2(s.media > 4.9 && s.media < 5.0, qPrintable(QString::number(s.media)));
        }
    }

    void gstStallAtTheStart()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        const auto run = runWithStallAtStart(1.0);
        // Whether the clock replays a stall before the first frame is its decision; the engine
        // follows the clock either way. With such a stall, the first frame stands for it.
        const auto spans = GstSegmentSource::makeSpans(PlaybackClock(*run));
        QVERIFY(spans.size() >= 3);
        const double lead = spans.front().stall ? spans.front().replayEnd : 0.0;
        QVERIFY(!spans.front().stall || std::abs(lead - 1.0) < 1e-9);
        auto source = ready(&sink, run);
        QVERIFY(source);
        QCOMPARE(source->position(), 0.0);
        QVERIFY(std::abs(source->frameMediaTime()) < 0.01);
        QVERIFY(seekAndShow(*source, lead > 0.0 ? lead / 2.0 : 0.5));
        QVERIFY(std::abs(source->frameMediaTime() - (lead > 0.0 ? 0.0 : 0.5)) < 0.01);
        QVERIFY(seekAndShow(*source, 0.0));

        Recorder recorder(*source, sink);
        source->play();
        QCOMPARE(source->state(), lead > 0.0 ? State::Stalled : State::Playing);
        QVERIFY(waitUntil([&] { return source->position() > lead + 1.0; }));
        source->pause();
        bool played = false;
        for (const Report& s : recorder.positions) {
            if (s.state == State::Stalled) {
                QVERIFY2(!played, "stalled again after the start");
                QVERIFY2(s.position <= lead + 1e-6, qPrintable(QString::number(s.position)));
                QVERIFY2(std::abs(s.media) < 0.01, qPrintable(QString::number(s.media)));
            } else if (s.state == State::Playing) {
                played = true;
                QVERIFY2(s.position >= lead - 1e-6, qPrintable(QString::number(s.position)));
                QVERIFY2(s.media < s.position - lead + 0.05, qPrintable(QStringLiteral("%1 at %2").arg(s.media).arg(s.position)));
            }
        }
        QVERIFY(played);
        for (const Report& s : recorder.frames) {
            if (s.state == State::Stalled)
                QVERIFY2(std::abs(s.media) < 0.01, qPrintable(QString::number(s.media)));
        }
        QVERIFY(source->frameMediaTime() > 0.5);
    }

    void gstReplayEndingInAStall()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink, runEndingInAStall());
        QVERIFY(source);
        // The end is inside the terminal stall: the last frame played stays.
        QVERIFY(seekAndShow(*source, 9.5));
        QCOMPARE(source->position(), 9.5);
        QVERIFY2(source->frameMediaTime() > 7.5 - kFrame - 0.005 && source->frameMediaTime() < 7.5,
                 qPrintable(QString::number(source->frameMediaTime())));
        QVERIFY(seekAndShow(*source, 8.2));
        Recorder recorder(*source, sink);
        source->play();
        QVERIFY(waitForState(*source, State::Stalled));
        QVERIFY(waitForState(*source, State::Ended));
        QCOMPARE(source->position(), 9.5);
        QVERIFY2(std::abs(source->frameMediaTime() - (7.5 - kFrame)) < 0.05, qPrintable(QString::number(source->frameMediaTime())));
        for (const Report& s : recorder.positions) {
            if (s.state == State::Stalled)
                QVERIFY2(s.media > 7.4 && s.media < 7.5, qPrintable(QString::number(s.media)));
        }
        // Playing again from the end starts over.
        source->play();
        QCOMPARE(source->state(), State::Playing);
        QVERIFY(source->position() < 0.1);
    }

    // ---- GStreamer engine: what listeners see --------------------------------------------

    void gstReportsAreConsistentThroughTheStall()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, 3.5));
        Recorder recorder(*source, sink);
        source->play();
        QVERIFY(waitForState(*source, State::Ended));
        QVERIFY(recorder.positions.size() > 20);
        QVERIFY(std::ranges::any_of(recorder.positions, [](const Report& s) { return s.state == State::Stalled; }));
        const QString failure = checkPlaythrough(recorder.positions, recorder.frames, 1.0);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    void gstLateTicksKeepStateAndFrameTogether_data()
    {
        QTest::addColumn<double>("blockAt");
        QTest::addColumn<int>("blockMs");
        // The event loop is blocked from about `blockAt` (replay time) for `blockMs`, so the
        // next tick is late: just before the stall ends, just before it starts, and across all
        // of it.
        QTest::newRow("before the stall ends") << 5.75 << 400;
        QTest::newRow("before the stall starts") << 4.75 << 400;
        QTest::newRow("across the whole stall") << 4.8 << 1500;
    }

    void gstLateTicksKeepStateAndFrameTogether()
    {
        REQUIRE_GSTREAMER_PASS();
        QFETCH(double, blockAt);
        QFETCH(int, blockMs);
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        QVERIFY(seekAndShow(*source, blockAt - 0.4));
        Recorder recorder(*source, sink);
        bool blocked = false;
        QObject::connect(source.get(), &ReplaySource::positionChanged, source.get(), [&](double position) {
            if (!blocked && position >= blockAt) {
                blocked = true;
                QThread::msleep(static_cast<unsigned long>(blockMs));
            }
        });
        source->play();
        QVERIFY(waitUntil([&] { return source->position() > 6.4; }));
        source->pause();
        QVERIFY(blocked);
        const QString failure = checkPlaythrough(recorder.positions, recorder.frames, 1.0);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    // ---- GStreamer engine: changing the run and deleting ---------------------------------

    void gstChangingTheRunAtAnyMoment()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        const auto other = test::syntheticRun(0.3, QStringLiteral("other"));
        // After the change: Paused at 0 with the new run's first frame, and nothing from the old
        // run arrives later.
        const auto settled = [&](GstSegmentSource& source) {
            if (!waitForState(source, State::Paused) || !waitUntil([&] { return source.framesShown() > 0; }))
                return QStringLiteral("not ready");
            QSignalSpy states(&source, &ReplaySource::stateChanged);
            QSignalSpy positions(&source, &ReplaySource::positionChanged);
            QTest::qWait(300);
            if (!states.isEmpty() || !positions.isEmpty())
                return QStringLiteral("%1 states, %2 positions later").arg(states.size()).arg(positions.size());
            if (source.position() != 0.0 || std::abs(source.frameMediaTime()) > 0.03)
                return QStringLiteral("position %1, media %2").arg(source.position()).arg(source.frameMediaTime());
            return QString();
        };

        {  // during a seek, before its frame
            auto source = ready(&sink);
            QVERIFY(source);
            source->seek(4.0);
            source->setRun(other);
            const QString failure = settled(*source);
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        {  // during a seek while playing
            auto source = ready(&sink);
            QVERIFY(source);
            source->play();
            source->seek(4.0);
            source->setRun(other);
            const QString failure = settled(*source);
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        {  // inside a stall while playing
            auto source = ready(&sink);
            QVERIFY(source);
            QVERIFY(seekAndShow(*source, 5.2));
            source->play();
            QTest::qWait(100);
            QCOMPARE(source->state(), State::Stalled);
            source->setRun(other);
            QCOMPARE(source->state(), State::Preparing);
            QCOMPARE(source->position(), 0.0);
            const QString failure = settled(*source);
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        for (const State trigger : {State::Stalled, State::Playing}) {
            // From the listener of the change into (or out of) the stall, inside a tick.
            auto source = ready(&sink);
            QVERIFY(source);
            QVERIFY(seekAndShow(*source, trigger == State::Stalled ? 4.8 : 5.8));
            bool replaced = false;
            QObject::connect(source.get(), &ReplaySource::stateChanged, source.get(), [&](State state) {
                if (state == trigger && !replaced && source->position() > 4.0) {
                    replaced = true;
                    source->setRun(other);
                }
            });
            source->play();
            QVERIFY(waitUntil([&] { return replaced; }));
            const QString failure = settled(*source);
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        {  // during the preroll, with play and a seek pending: neither carries over
            auto source = std::make_unique<GstSegmentSource>();
            source->setVideoSink(&sink);
            source->setRun(test::syntheticRun());
            source->play();
            source->seek(3.0);
            source->setRun(other);
            const QString failure = settled(*source);
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        {  // many times in a row, sometimes letting the events through
            auto source = std::make_unique<GstSegmentSource>();
            source->setVideoSink(&sink);
            for (int i = 0; i < 20; ++i) {
                source->setRun(i % 2 ? other : test::syntheticRun());
                if (i % 3 == 0)
                    source->play();
                QTest::qWait(i % 4 == 0 ? 0 : 5 * i);
            }
            source->setRun(other);
            const QString failure = settled(*source);
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
        }
        {  // to no run during the preroll: stays idle
            auto source = std::make_unique<GstSegmentSource>();
            source->setVideoSink(&sink);
            source->setRun(test::syntheticRun());
            source->setRun(nullptr);
            QCOMPARE(source->state(), State::Idle);
            QVERIFY(!source->isReady());
            QSignalSpy states(source.get(), &ReplaySource::stateChanged);
            QTest::qWait(500);
            QVERIFY(states.isEmpty());
            source->play();
            source->seek(2.0);
            QCOMPARE(source->state(), State::Idle);
            QCOMPARE(source->position(), 0.0);
        }
    }

    void gstEventsFromTheOldPipelineDoNotReachTheNewRun()
    {
        REQUIRE_GSTREAMER_PASS();
        // The old pipeline's bus messages are queued for the GUI thread while it is blocked;
        // then the run changes. They must not be taken for the new pipeline's.
        {  // an old error must not fail a good run
            GstSegmentSource source;
            source.setRun(corruptRun());
            QThread::msleep(2000);  // the error is posted meanwhile
            QSignalSpy errors(&source, &ReplaySource::errorOccurred);
            source.setRun(test::syntheticRun());
            QVERIFY(waitForState(source, State::Paused));
            QTest::qWait(300);
            QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
            QCOMPARE(source.state(), State::Paused);
        }
        {  // an old preroll must not make a broken run look ready
            GstSegmentSource source;
            source.setRun(test::syntheticRun());
            QThread::msleep(2000);  // the preroll completes meanwhile
            QSignalSpy states(&source, &ReplaySource::stateChanged);
            QSignalSpy errors(&source, &ReplaySource::errorOccurred);
            source.setRun(corruptRun());
            QVERIFY(waitForState(source, State::Error));
            for (const auto& args : states)
                QVERIFY2(args.first().value<State>() != State::Paused, "an old preroll reached the new run");
            QTest::qWait(300);
            QCOMPARE(errors.size(), 1);
        }
    }

    void gstDeletingAtAnyMoment()
    {
        REQUIRE_GSTREAMER_PASS();
        QVideoSink sink;
        {  // right after a seek, before its frame
            auto source = ready(&sink);
            QVERIFY(source);
            source->seek(4.0);
            source.reset();
        }
        {  // right after a storm of seeks while playing
            auto source = ready(&sink);
            QVERIFY(source);
            source->play();
            for (const double t : stormTargets(100, 0.0, 7.25, 11))
                source->seek(t);
            source.reset();
        }
        {  // inside a stall
            auto source = ready(&sink, 2.0);
            QVERIFY(source);
            QVERIFY(seekAndShow(*source, 5.2));
            source->play();
            QTest::qWait(150);
            QCOMPARE(source->state(), State::Stalled);
            source.reset();
        }
        {  // just after Ended
            auto source = ready(&sink);
            QVERIFY(source);
            QVERIFY(seekAndShow(*source, 7.1));
            source->play();
            QVERIFY(waitForState(*source, State::Ended));
            source.reset();
        }
        {  // from its own position report while playing (deleteLater)
            QPointer<GstSegmentSource> source = ready(&sink).release();
            QVERIFY(source);
            QObject::connect(source.data(), &ReplaySource::positionChanged, source.data(), [&](double position) {
                if (position > 0.2)
                    source->deleteLater();
            });
            source->play();
            QVERIFY(waitUntil([&] { return source.isNull(); }));
        }
        {  // from its own stall report
            QPointer<GstSegmentSource> source = ready(&sink).release();
            QVERIFY(source);
            QVERIFY(seekAndShow(*source, 4.8));
            QObject::connect(source.data(), &ReplaySource::stateChanged, source.data(), [&](State state) {
                if (state == State::Stalled)
                    source->deleteLater();
            });
            source->play();
            QVERIFY(waitUntil([&] { return source.isNull(); }));
        }
        {  // the sink goes first
            auto sink2 = std::make_unique<QVideoSink>();
            auto source = ready(sink2.get());
            QVERIFY(source);
            source->play();
            QTest::qWait(100);
            sink2.reset();
            QTest::qWait(200);
            QVERIFY(source->state() == State::Playing);
            const qint64 shown = source->framesShown();
            QVERIFY(waitUntil([&] { return source->framesShown() > shown; }));  // counted, not shown
            source.reset();
        }
    }

    // ---- GStreamer engine: broken media --------------------------------------------------

    void gstCorruptMediaIsOneError_data()
    {
        QTest::addColumn<int>("segment");
        QTest::newRow("every segment") << -1;
        QTest::newRow("the first segment") << 0;
        QTest::newRow("the last segment") << 2;
    }

    void gstCorruptMediaIsOneError()
    {
        REQUIRE_GSTREAMER_PASS();
        QFETCH(int, segment);
        QVideoSink sink;
        GstSegmentSource source;
        source.setVideoSink(&sink);
        QSignalSpy errors(&source, &ReplaySource::errorOccurred);
        QSignalSpy states(&source, &ReplaySource::stateChanged);
        source.setRun(corruptRun(segment));
        QVERIFY(waitForState(source, State::Error));
        QTest::qWait(1000);  // any further error message
        QCOMPARE(errors.size(), 1);
        QVERIFY(!errors.first().first().toString().isEmpty());
        QCOMPARE(states.last().first().value<State>(), State::Error);
        // Nothing revives a failed engine but a new run.
        source.play();
        source.seek(3.0);
        source.pause();
        source.play();
        QTest::qWait(200);
        QCOMPARE(source.state(), State::Error);
        QCOMPARE(errors.size(), 1);
        source.setRun(test::syntheticRun());
        QVERIFY(waitForState(source, State::Paused));
    }

    void gstMissingMediaIsOneError()
    {
        REQUIRE_GSTREAMER_PASS();
        for (const QString& path : {QString(), QStringLiteral("/no/such/dir/"), test::dataPath(QStringLiteral("media"))}) {
            auto run = std::make_shared<RunData>(*test::syntheticRun());
            run->segments[2].mediaPath = path;
            GstSegmentSource source;
            QSignalSpy errors(&source, &ReplaySource::errorOccurred);
            source.setRun(run);
            QVERIFY2(waitForState(source, State::Error), qPrintable(path));
            QTest::qWait(300);
            QCOMPARE(errors.size(), 1);
        }
    }
#endif // ABRVIZ_WITH_GSTREAMER

    // ---- Pre-rendered engine --------------------------------------------------------------

    void prerenderedSeekToEdgeTimes()
    {
        REQUIRE_PRERENDERED_PASS();
        QVideoSink sink;
        auto source = rendered(&sink, test::syntheticRun());
        QVERIFY(source);
        QSignalSpy errors(source.get(), &ReplaySource::errorOccurred);
        struct Case {
            double target;
            double position;
        };
        const Case cases[] = {{-5.0, 0.0}, {0.0, 0.0}, {-kInf, 0.0}, {5.0, 5.0}, {6.0, 6.0},
                              {7.25, 7.25}, {100.0, 7.25}, {kInf, 7.25}};
        for (const Case& c : cases) {
            source->seek(2.0);
            QTRY_VERIFY_WITH_TIMEOUT(std::abs(source->position() - 2.0) < 0.05, kTimeoutMs);
            source->seek(c.target);
            QTRY_VERIFY2_WITH_TIMEOUT(std::abs(source->position() - c.position) < 0.07,
                                      qPrintable(QStringLiteral("seek %1: position %2").arg(c.target).arg(source->position())),
                                      kTimeoutMs);
            QTest::qWait(600);  // the paused seek's scrub ends
            QVERIFY2(std::abs(source->position() - c.position) < 0.07,
                     qPrintable(QStringLiteral("seek %1: position %2 later").arg(c.target).arg(source->position())));
            QVERIFY2(source->state() == State::Paused || source->state() == State::Ended,
                     qPrintable(QStringLiteral("seek %1: state %2").arg(c.target).arg(int(source->state()))));
        }
        // A NaN seek is ignored.
        source->seek(2.0);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(source->position() - 2.0) < 0.05, kTimeoutMs);
        QTest::qWait(600);
        source->seek(kNaN);
        QTest::qWait(600);
        QVERIFY2(std::abs(source->position() - 2.0) < 0.05, qPrintable(QString::number(source->position())));
        QCOMPARE(source->state(), State::Paused);
        QVERIFY(errors.isEmpty());
    }

    void prerenderedSeekStorm()
    {
        REQUIRE_PRERENDERED_PASS();
        QVideoSink sink;
        auto source = rendered(&sink, test::syntheticRun());
        QVERIFY(source);
        QSignalSpy errors(source.get(), &ReplaySource::errorOccurred);

        // While paused: the storm ends where the last seek went, still paused.
        int n = 0;
        for (const double t : stormTargets(300, 0.0, 7.25, 3)) {
            source->seek(t);
            if (++n % 7 == 0)
                QCoreApplication::processEvents();
        }
        source->seek(2.0);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(source->position() - 2.0) < 0.05 && source->state() == State::Paused, kTimeoutMs);
        QTest::qWait(800);
        QVERIFY2(std::abs(source->position() - 2.0) < 0.05, qPrintable(QString::number(source->position())));
        QCOMPARE(source->state(), State::Paused);

        // While playing: playing on from the last seek.
        source->play();
        QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Playing, kTimeoutMs);
        for (const double t : stormTargets(300, 2.5, 7.0, 5)) {
            source->seek(t);
            if (++n % 7 == 0)
                QCoreApplication::processEvents();
        }
        source->seek(1.0);
        QTest::qWait(300);
        QTRY_VERIFY_WITH_TIMEOUT(source->position() > 1.0 && source->position() < 2.5, kTimeoutMs);
        QCOMPARE(source->state(), State::Playing);
        QVERIFY(errors.isEmpty());
    }

    void prerenderedPlayPauseSpam()
    {
        REQUIRE_PRERENDERED_PASS();
        QVideoSink sink;
        auto source = rendered(&sink, test::syntheticRun());
        QVERIFY(source);
        QSignalSpy errors(source.get(), &ReplaySource::errorOccurred);
        source->seek(1.0);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(source->position() - 1.0) < 0.05, kTimeoutMs);
        for (int i = 0; i < 300; ++i) {
            source->play();
            if (i % 3 == 0)
                QCoreApplication::processEvents();
            source->pause();
            if (i % 5 == 0)
                QCoreApplication::processEvents();
        }
        QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Paused, kTimeoutMs);
        QTest::qWait(300);
        const double paused = source->position();
        QTest::qWait(300);
        QCOMPARE(source->position(), paused);
        QCOMPARE(source->state(), State::Paused);
        for (int i = 0; i < 50; ++i) {
            source->pause();
            source->play();
        }
        QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Playing, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(source->position() > paused + 0.3, kTimeoutMs);
        QVERIFY(errors.isEmpty());
    }

    void prerenderedPlayWhenEndedRestarts()
    {
        REQUIRE_PRERENDERED_PASS();
        QVideoSink sink;
        auto source = rendered(&sink, test::syntheticRun());
        QVERIFY(source);
        source->seek(6.9);
        source->play();
        QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Ended, kTimeoutMs);
        QVERIFY(std::abs(source->position() - 7.25) < 0.1);
        source->play();
        QTRY_VERIFY_WITH_TIMEOUT(source->state() == State::Playing && source->position() < 3.0, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(source->position() > 0.3, kTimeoutMs);

        // Paused exactly at the end, play starts again from 0 too (as the GStreamer engine).
        source->pause();
        source->seek(7.25);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(source->position() - 7.25) < 0.07, kTimeoutMs);
        QTest::qWait(600);
        QSignalSpy states(source.get(), &ReplaySource::stateChanged);
        const State before = source->state();
        source->play();
        QTRY_VERIFY2_WITH_TIMEOUT(source->state() == State::Playing && source->position() < 3.0,
                                  qPrintable(QStringLiteral("from state %1: state %2, position %3, %4 changes")
                                                 .arg(int(before)).arg(int(source->state()))
                                                 .arg(source->position()).arg(states.size())),
                                  kTimeoutMs);
    }

    void prerenderedChangingTheRunAndDeleting()
    {
        REQUIRE_PRERENDERED_PASS();
        QVideoSink sink;
        const auto run = test::syntheticRun();
        QVERIFY(rendered(&sink, run));  // in the cache from here on
        {  // during the load of a cached file
            auto source = std::make_unique<PrerenderedSource>(queue_.get());
            source->setVideoSink(&sink);
            source->setRun(run);
            QVERIFY(source->isReady());
            source.reset();
        }
        {  // during a render
            auto source = std::make_unique<PrerenderedSource>(queue_.get());
            source->setVideoSink(&sink);
            source->setRun(test::syntheticRun(0.7, QStringLiteral("never-rendered")));
            source->prepare();
            QCOMPARE(source->state(), State::Preparing);
            QTest::qWait(50);
            source.reset();
        }
        {  // during a paused seek's scrub, then changing the run during another
            auto source = rendered(&sink, run);
            QVERIFY(source);
            source->seek(3.0);
            source->setRun(run);
            QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Paused, kTimeoutMs);
            QTest::qWait(700);
            QVERIFY2(std::abs(source->position()) < 0.05, qPrintable(QString::number(source->position())));
            QCOMPARE(source->state(), State::Paused);
            source->seek(4.0);
            source.reset();
        }
        {  // during playback, with a pending play and seek: neither carries over
            auto source = rendered(&sink, run);
            QVERIFY(source);
            source->play();
            QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Playing, kTimeoutMs);
            source->setRun(run);
            QCOMPARE(source->position(), 0.0);
            QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Paused, kTimeoutMs);
            QTest::qWait(500);
            QCOMPARE(source->state(), State::Paused);
            QVERIFY(source->position() < 0.05);
            source->play();
            QTRY_COMPARE_WITH_TIMEOUT(source->state(), State::Playing, kTimeoutMs);
            source.reset();
        }
    }

    void prerenderedCorruptMediaIsOneError()
    {
        REQUIRE_PRERENDERED_PASS();
        PrerenderedSource source(queue_.get());
        QSignalSpy errors(&source, &ReplaySource::errorOccurred);
        source.setRun(corruptRun());
        source.prepare();
        QTRY_COMPARE_WITH_TIMEOUT(source.state(), State::Error, 60000);
        QTest::qWait(500);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!source.isReady());
        source.play();
        source.seek(2.0);
        QTest::qWait(200);
        QCOMPARE(source.state(), State::Error);
        QCOMPARE(errors.size(), 1);
    }

    // ---- Both engines ----------------------------------------------------------------------

    void bothEnginesShowTheSameMoment()
    {
#if ABRVIZ_WITH_GSTREAMER
        REQUIRE_PRERENDERED_PASS();
        const auto run = test::syntheticRun();
        // Within the first segment, the stall (the last frame before it) and the cut-off
        // segment; at least 0.4 s of the clip apart from each other.
        const std::vector<double> times = {0.2, 1.0, 1.8, 5.5, 6.6};
        std::vector<QImage> pictures[2];
        {
            QVideoSink sink;
            auto source = ready(&sink, run);
            QVERIFY(source);
            for (const double time : times) {
                QVERIFY(seekAndShow(*source, time));
                pictures[0].push_back(thumbnail(sink));
            }
        }
        {
            QVideoSink sink;
            auto source = rendered(&sink, run);
            QVERIFY(source);
            for (const double time : times) {
                source->seek(time);
                QTRY_VERIFY_WITH_TIMEOUT(std::abs(source->position() - time) < 0.05, kTimeoutMs);
                QTest::qWait(800);  // the frame at the new position (and the scrub's pause)
                pictures[1].push_back(thumbnail(sink));
            }
        }
        for (std::size_t i = 0; i < times.size(); ++i) {
            const double same = difference(pictures[0][i], pictures[1][i]);
            double others = 255.0;
            for (std::size_t j = 0; j < times.size(); ++j) {
                if (j != i)
                    others = std::min(others, difference(pictures[0][i], pictures[1][j]));
            }
            QVERIFY2(same < others / 2.0, qPrintable(QStringLiteral("replay %1: same %2, closest other %3")
                                                          .arg(times[i]).arg(same).arg(others)));
        }
#else
        QSKIP("built without GStreamer");
#endif
    }
};

QTEST_MAIN(TestEngineEdgeCases)
#include "tst_engine_edge_cases.moc"
