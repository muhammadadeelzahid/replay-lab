#include "abrviz/core/playback/playback_clock.h"

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

constexpr double kFrameDuration = 0.033;  ///< Seconds per frame of the simulated player (33 ms).

/**
 * @brief Returns the start of a piece on one timeline.
 *
 * @param p The piece.
 * @param axis 0 for simulation, 1 for media, anything else for replay time.
 * @return The start in seconds.
 */
double start(const PlaybackClock::Piece& p, int axis)
{
    switch (axis) {
    case 0: return p.simStart;
    case 1: return p.mediaStart;
    default: return p.replayStart;
    }
}

/**
 * @brief Returns the end of a piece on one timeline.
 *
 * @param p The piece.
 * @param axis 0 for simulation, 1 for media, anything else for replay time.
 * @return The end in seconds.
 */
double end(const PlaybackClock::Piece& p, int axis)
{
    switch (axis) {
    case 0: return p.simEnd;
    case 1: return p.mediaEnd;
    default: return p.replayEnd;
    }
}

/**
 * @brief Returns a length that can go on a timeline.
 *
 * @param seconds A duration from the run.
 * @return `seconds`, or 0 if it is negative, infinite or NaN.
 */
double validLength(double seconds)
{
    return std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
}

/**
 * @brief Finds a downloaded segment.
 *
 * @param run The run.
 * @param segment Zero-based segment index.
 * @return The segment, or null if it was not downloaded.
 */
const Segment* findSegment(const RunData& run, int segment)
{
    if (segment < 0)
        return nullptr;
    const auto index = static_cast<std::size_t>(segment);
    // `segments` is in index order, normally with segment i at position i.
    if (index < run.segments.size() && run.segments[index].index == segment)
        return &run.segments[index];
    const auto it = std::ranges::find(run.segments, segment, &Segment::index);
    return it == run.segments.end() ? nullptr : &*it;
}

} // namespace

PlaybackClock::PlaybackClock(const RunData& run)
{
    // Segments and stalls on the simulation timeline, in order.
    struct Event {
        double simStart;
        bool isStall;
        std::size_t index;
    };
    // Times that are not finite cannot be placed on the timeline (and NaN would break the sort).
    std::vector<Event> events;
    for (std::size_t i = 0; i < run.playback.size(); ++i) {
        if (std::isfinite(run.playback[i].firstFrameTime))
            events.push_back({run.playback[i].firstFrameTime, false, i});
    }
    for (std::size_t i = 0; i < run.stalls.size(); ++i) {
        if (std::isfinite(run.stalls[i].start))
            events.push_back({run.stalls[i].start, true, i});
    }
    std::ranges::stable_sort(events, {}, &Event::simStart);
    // A stall before the first frame of a downloaded segment has no frame to freeze: it is
    // start-up, not replayed (the render plan drops it too).
    const auto firstShown = std::ranges::find_if(events, [&run](const Event& e) {
        return !e.isStall && findSegment(run, run.playback[e.index].segment);
    });
    events.erase(std::remove_if(events.begin(), firstShown, [](const Event& e) { return e.isStall; }),
                 firstShown);

    const bool cutOff = run.playbackCutOff();
    double media = 0.0;
    double replay = 0.0;
    int lastSegment = -1;
    for (std::size_t e = 0; e < events.size(); ++e) {
        const Event& event = events[e];
        Piece piece;
        piece.simStart = event.simStart;

        if (event.isStall) {
            const Stall& stall = run.stalls[event.index];
            piece.kind = Piece::Kind::Stall;
            piece.segment = lastSegment;
            // Ends at the next piece at the latest, so pieces never overlap in simulation time.
            piece.simEnd = std::isfinite(stall.end) ? stall.end : piece.simStart;
            if (e + 1 < events.size())
                piece.simEnd = std::min(piece.simEnd, events[e + 1].simStart);
            piece.mediaStart = piece.mediaEnd = media;
            piece.replayStart = replay;
            replay += validLength(stall.duration());
            piece.replayEnd = replay;
        } else {
            const SegmentPlayback& p = run.playback[event.index];
            const bool last = event.index + 1 == run.playback.size();
            piece.kind = Piece::Kind::Segment;
            piece.segment = p.segment;
            lastSegment = p.segment;
            // A segment lasts until the next piece starts; the last one until its last frame.
            piece.simEnd = e + 1 < events.size() ? events[e + 1].simStart
                                                 : p.lastFrameTime + kFrameDuration;

            if (!std::isfinite(piece.simEnd))
                piece.simEnd = piece.simStart;

            const Segment* segment = findSegment(run, p.segment);
            double duration = segment ? validLength(segment->mediaDuration) : 0.0;
            if (last && cutOff)
                duration *= std::clamp(static_cast<double>(p.framesPlayed) / kFramesPerSegment,
                                       0.0, 1.0);
            piece.mediaStart = media;
            media += duration;
            piece.mediaEnd = media;
            piece.replayStart = replay;
            replay += duration;
            piece.replayEnd = replay;
        }
        piece.simEnd = std::max(piece.simEnd, piece.simStart);
        pieces_.push_back(piece);
    }
}

const PlaybackClock::Piece* PlaybackClock::pieceAt(double value, Axis axis) const
{
    if (pieces_.empty())
        return nullptr;
    const int a = static_cast<int>(axis);
    // The last piece whose start is not after `value`. Stalls take no media time, so on the media
    // axis the segment after a stall (same start) is preferred.
    const auto it = std::ranges::upper_bound(pieces_, value, {},
                                             [a](const Piece& p) { return start(p, a); });
    if (it == pieces_.begin())
        return nullptr;
    return &*std::prev(it);
}

double PlaybackClock::convert(double value, Axis from, Axis to) const
{
    if (pieces_.empty())
        return 0.0;
    const int f = static_cast<int>(from);
    const int t = static_cast<int>(to);
    const Piece* piece = std::isnan(value) ? nullptr : pieceAt(value, from);
    if (!piece)
        return start(pieces_.front(), t);
    if (value >= end(pieces_.back(), f))
        return end(pieces_.back(), t);

    const double length = end(*piece, f) - start(*piece, f);
    const double fraction = length > 0.0 ? (value - start(*piece, f)) / length : 0.0;
    return start(*piece, t) + std::clamp(fraction, 0.0, 1.0) * (end(*piece, t) - start(*piece, t));
}

double PlaybackClock::simToReplay(double simTime) const
{
    return convert(simTime, Axis::Sim, Axis::Replay);
}

double PlaybackClock::replayToSim(double replayTime) const
{
    return convert(replayTime, Axis::Replay, Axis::Sim);
}

double PlaybackClock::replayToMedia(double replayTime) const
{
    return convert(replayTime, Axis::Replay, Axis::Media);
}

double PlaybackClock::mediaToReplay(double mediaTime) const
{
    return convert(mediaTime, Axis::Media, Axis::Replay);
}

PlaybackClock::Position PlaybackClock::positionIn(const Piece& piece, double value,
                                                  Axis axis) const
{
    const int a = static_cast<int>(axis);
    Position position;
    position.segment = piece.segment;
    position.stalled = piece.kind == Piece::Kind::Stall;
    if (position.stalled) {
        position.fraction = 1.0;  // the last frame of the previous segment stays on screen
        return position;
    }
    const double length = end(piece, a) - start(piece, a);
    position.fraction =
        length > 0.0 ? std::clamp((value - start(piece, a)) / length, 0.0, 1.0) : 0.0;
    return position;
}

PlaybackClock::Position PlaybackClock::positionAtSim(double simTime) const
{
    Position position;
    if (pieces_.empty() || std::isnan(simTime) || simTime < pieces_.front().simStart) {
        position.beforeStart = true;
        return position;
    }
    if (simTime >= pieces_.back().simEnd) {
        position = positionIn(pieces_.back(), pieces_.back().simEnd, Axis::Sim);
        position.afterEnd = true;
        return position;
    }
    return positionIn(*pieceAt(simTime, Axis::Sim), simTime, Axis::Sim);
}

PlaybackClock::Position PlaybackClock::positionAtReplay(double replayTime) const
{
    Position position;
    if (pieces_.empty() || std::isnan(replayTime) || replayTime < 0.0) {
        position.beforeStart = true;
        return position;
    }
    if (replayTime >= pieces_.back().replayEnd) {
        position = positionIn(pieces_.back(), pieces_.back().replayEnd, Axis::Replay);
        position.afterEnd = true;
        return position;
    }
    return positionIn(*pieceAt(replayTime, Axis::Replay), replayTime, Axis::Replay);
}

double PlaybackClock::replayDuration() const
{
    return pieces_.empty() ? 0.0 : pieces_.back().replayEnd;
}

double PlaybackClock::mediaDuration() const
{
    return pieces_.empty() ? 0.0 : pieces_.back().mediaEnd;
}

double PlaybackClock::simStart() const
{
    return pieces_.empty() ? 0.0 : pieces_.front().simStart;
}

double PlaybackClock::simEnd() const
{
    return pieces_.empty() ? 0.0 : pieces_.back().simEnd;
}

} // namespace abrviz
