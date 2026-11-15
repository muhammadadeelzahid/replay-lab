#pragma once

/**
 * @file
 * @brief Mapping between the simulation, media and replay timelines of a session.
 */

#include "abrviz/core/model/run_data.h"

#include <span>
#include <vector>

namespace abrviz {

/**
 * @brief Maps between the three timelines of a session.
 *
 * See docs/architecture.md, "Playback clock":
 * - simulation time: the times in the bundle and the logs;
 * - media time: the played segments' media back to back, without stalls;
 * - replay time: media time with a frozen frame for every stall.
 *
 * The clock is a list of pieces in playback order. A segment lasts from its first frame to the
 * next piece in simulation time, and for its real media duration in media and replay time. A
 * stall lasts its duration in simulation and replay time and takes no media time. Within a
 * piece the mapping is linear; outside the session times are clamped.
 *
 * Stalls before the first downloaded segment is shown are start-up and not part of the replay. A stall ends at the
 * next piece at the latest in simulation time, so pieces never overlap. Records with times that
 * are not finite are left out, and invalid durations count as 0. A NaN time maps like a time
 * before the session.
 */
class PlaybackClock {
public:
    /**
     * @brief One segment or stall, with its extent on each timeline (seconds).
     */
    struct Piece {
        /** @brief Whether the piece shows a segment or a stall. */
        enum class Kind { Segment, Stall };
        Kind kind = Kind::Segment;  ///< segment or stall
        int segment = -1;  ///< the segment shown (for a stall: the last one shown before it)
        /** @brief Simulation time: first frame of the segment, or start of the stall. */
        double simStart = 0.0;
        /** @brief Simulation time: start of the next piece (last segment: last frame + 33 ms). */
        double simEnd = 0.0;
        double mediaStart = 0.0;   ///< media time at the start
        double mediaEnd = 0.0;     ///< media time at the end; equals `mediaStart` for a stall
        double replayStart = 0.0;  ///< replay time at the start
        double replayEnd = 0.0;    ///< replay time at the end
    };

    /**
     * @brief What is on screen at a point in time.
     */
    struct Position {
        int segment = -1;       ///< -1 before playback starts
        double fraction = 0.0;  ///< 0..1 within the segment's media
        bool stalled = false;      ///< a stall: the last frame of `segment` stays on screen
        bool beforeStart = false;  ///< the time is before the first piece
        bool afterEnd = false;     ///< the time is at or after the end of the last piece
    };

    /**
     * @brief Creates an empty clock.
     */
    PlaybackClock() = default;

    /**
     * @brief Builds the pieces from the run's playback and stalls.
     *
     * The last segment lasts until its last frame (plus one frame) in simulation time. If the
     * run was cut off, its media duration is shortened to the fraction of frames played.
     *
     * @param run The loaded run.
     */
    explicit PlaybackClock(const RunData& run);

    /**
     * @brief Converts simulation time to replay time.
     *
     * @param simTime Simulation time in seconds.
     * @return Replay time in seconds, clamped to the session; 0 for an empty clock.
     */
    [[nodiscard]] double simToReplay(double simTime) const;

    /**
     * @brief Converts replay time to simulation time.
     *
     * @param replayTime Replay time in seconds.
     * @return Simulation time in seconds, clamped to the session; 0 for an empty clock.
     */
    [[nodiscard]] double replayToSim(double replayTime) const;

    /**
     * @brief Converts replay time to media time.
     *
     * During a stall the media time stays at the end of the previous segment.
     *
     * @param replayTime Replay time in seconds.
     * @return Media time in seconds, clamped to the session; 0 for an empty clock.
     */
    [[nodiscard]] double replayToMedia(double replayTime) const;

    /**
     * @brief Converts media time to replay time.
     *
     * At a stall boundary the segment after the stall is used.
     *
     * @param mediaTime Media time in seconds.
     * @return Replay time in seconds, clamped to the session; 0 for an empty clock.
     */
    [[nodiscard]] double mediaToReplay(double mediaTime) const;

    /**
     * @brief Returns what is on screen at a simulation time.
     *
     * @param simTime Simulation time in seconds.
     * @return The position; `beforeStart` before the first piece or for an empty clock,
     *         `afterEnd` (with the end of the last piece) from the end of the session.
     */
    [[nodiscard]] Position positionAtSim(double simTime) const;

    /**
     * @brief Returns what is on screen at a replay time.
     *
     * @param replayTime Replay time in seconds.
     * @return The position; `beforeStart` for negative times or an empty clock, `afterEnd`
     *         (with the end of the last piece) from the end of the replay.
     */
    [[nodiscard]] Position positionAtReplay(double replayTime) const;

    /**
     * @brief Returns the length of the replay.
     *
     * @return Seconds, stalls included; 0 for an empty clock.
     */
    [[nodiscard]] double replayDuration() const;

    /**
     * @brief Returns the length of the played media.
     *
     * @return Seconds, stalls excluded; 0 for an empty clock.
     */
    [[nodiscard]] double mediaDuration() const;

    /**
     * @brief Returns the simulation time at which playback starts.
     *
     * @return Start of the first piece in seconds; 0 for an empty clock.
     */
    [[nodiscard]] double simStart() const;

    /**
     * @brief Returns the simulation time at which playback ends.
     *
     * @return End of the last piece in seconds; 0 for an empty clock.
     */
    [[nodiscard]] double simEnd() const;

    /**
     * @brief Tells whether the clock has no pieces.
     *
     * @return True if nothing was played.
     */
    [[nodiscard]] bool isEmpty() const { return pieces_.empty(); }

    /**
     * @brief Returns the pieces in playback order.
     *
     * @return The pieces.
     */
    [[nodiscard]] std::span<const Piece> pieces() const { return pieces_; }

private:
    /** @brief A timeline; the values index the start and end fields of a piece. */
    enum class Axis { Sim, Media, Replay };

    /**
     * @brief Converts a time between two timelines.
     *
     * Linear within the piece that contains `value`; clamped to the first and last pieces.
     *
     * @param value Time on the `from` timeline, in seconds.
     * @param from Timeline of `value`.
     * @param to Timeline of the result.
     * @return Time on the `to` timeline; 0 for an empty clock.
     */
    [[nodiscard]] double convert(double value, Axis from, Axis to) const;

    /**
     * @brief Returns the position at a time within one piece.
     *
     * In a stall the fraction is 1: the last frame of the previous segment stays on screen.
     *
     * @param piece The piece that contains `value`.
     * @param value Time on `axis`, in seconds.
     * @param axis Timeline of `value`.
     * @return The position.
     */
    [[nodiscard]] Position positionIn(const Piece& piece, double value, Axis axis) const;

    /**
     * @brief Returns the last piece whose start is not after `value`.
     *
     * Stalls take no media time, so on the media axis the segment after a stall (same start)
     * is preferred.
     *
     * @param value Time on `axis`, in seconds.
     * @param axis Timeline of `value`.
     * @return The piece, or null if `value` is before the first piece or the clock is empty.
     */
    [[nodiscard]] const Piece* pieceAt(double value, Axis axis) const;

    std::vector<Piece> pieces_;  ///< segments and stalls in playback order
};

} // namespace abrviz
