#pragma once

#include "abrviz/core/model/run_data.h"

#include <span>
#include <vector>

namespace abrviz {

/// Maps between the three timelines of a session (see docs/architecture.md, "Playback clock"):
/// - simulation time: the times in the bundle and the logs;
/// - media time: the played segments' media back to back, without stalls;
/// - replay time: media time with a frozen frame for every stall.
///
/// The clock is a list of pieces in playback order. A segment lasts from its first frame to the
/// next piece in simulation time, and for its real media duration in media and replay time. A
/// stall lasts its duration in simulation and replay time and takes no media time. Within a
/// piece the mapping is linear; outside the session times are clamped.
class PlaybackClock {
public:
    struct Piece {
        enum class Kind { Segment, Stall };
        Kind kind = Kind::Segment;
        int segment = -1;  ///< the segment shown (for a stall: the last one shown before it)
        double simStart = 0.0;
        double simEnd = 0.0;
        double mediaStart = 0.0;
        double mediaEnd = 0.0;
        double replayStart = 0.0;
        double replayEnd = 0.0;
    };

    struct Position {
        int segment = -1;       ///< -1 before playback starts
        double fraction = 0.0;  ///< 0..1 within the segment's media
        bool stalled = false;
        bool beforeStart = false;
        bool afterEnd = false;
    };

    PlaybackClock() = default;
    explicit PlaybackClock(const RunData& run);

    [[nodiscard]] double simToReplay(double simTime) const;
    [[nodiscard]] double replayToSim(double replayTime) const;
    [[nodiscard]] double replayToMedia(double replayTime) const;
    [[nodiscard]] double mediaToReplay(double mediaTime) const;

    [[nodiscard]] Position positionAtSim(double simTime) const;
    [[nodiscard]] Position positionAtReplay(double replayTime) const;

    [[nodiscard]] double replayDuration() const;
    [[nodiscard]] double mediaDuration() const;
    [[nodiscard]] double simStart() const;
    [[nodiscard]] double simEnd() const;
    [[nodiscard]] bool isEmpty() const { return pieces_.empty(); }
    [[nodiscard]] std::span<const Piece> pieces() const { return pieces_; }

private:
    enum class Axis { Sim, Media, Replay };
    [[nodiscard]] double convert(double value, Axis from, Axis to) const;
    [[nodiscard]] Position positionIn(const Piece& piece, double value, Axis axis) const;
    [[nodiscard]] const Piece* pieceAt(double value, Axis axis) const;

    std::vector<Piece> pieces_;
};

} // namespace abrviz
