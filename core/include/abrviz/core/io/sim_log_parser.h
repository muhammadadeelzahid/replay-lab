#pragma once

/**
 * @file
 * @brief Parser for the decision, frame and pause lines of the ns-3 `sim.log`.
 */

#include "abrviz/core/model/run_data.h"

#include <QByteArrayView>

#include <vector>

namespace abrviz {

/**
 * @brief A decision line of `sim.log`.
 *
 * Format: `<t> ue-id: N videoId: V segmentId: k newBitRate: X oldBitRate: Y ...`.
 * `segment` has just finished downloading at rate `oldBitRate`; `newBitRate` is the rate
 * chosen for segment `segment + 1`. Optional fields missing from the line are 0.
 */
struct SimLogDecision {
    double time = 0.0;      ///< simulation time in seconds (first word)
    int segment = 0;        ///< `segmentId`: zero-based segment just downloaded
    qint64 newBitRate = 0;  ///< `newBitRate`: bits per second chosen for `segment + 1`
    qint64 oldBitRate = 0;  ///< `oldBitRate`: bits per second `segment` was downloaded at
    double throughputEstimate = 0.0;  ///< `estBitRate`
    double interruptionTime = 0.0;    ///< `interTime`
    double bufferLevel = 0.0;         ///< `T`
    double bufferChange = 0.0;        ///< `dT`
    double bufferFullWait = 0.0;      ///< `del`
    double fetchTime = 0.0;           ///< `fetch`
    double lossRate = 0.0;            ///< `loss`
};

/**
 * @brief A `[MPEG_PLAYER]` pause line ("No frames to play").
 */
struct PlayerPause {
    double time = 0.0;  ///< Simulation time of the pause, in seconds (`t=`).
};

/**
 * @brief A `[MPEG_PLAYER]` resume line ("Play resumed").
 */
struct PlayerResume {
    double time = 0.0;  ///< Simulation time of the resume, in seconds (`t=`).
    double pauseDuration = 0.0;  ///< `duration=` value in seconds; 0 if missing.
};

/**
 * @brief What `sim.log` says about one UE.
 *
 * Produced per chunk and merged in order.
 *
 * @see SimLogParser::merge()
 */
struct SimLogPart {
    std::vector<SimLogDecision> decisions;  ///< Decision lines of the UE, in log order.
    std::vector<BufferPoint> buffer;  ///< Buffer level at every `PLAYING FRAME` line, in log order.
    /** @brief One entry per run of frames of the same segment, in log order. */
    std::vector<SegmentPlayback> playback;
    std::vector<PlayerPause> pauses;        ///< Pause lines of the UE's player, in log order.
    std::vector<PlayerResume> resumes;      ///< Resume lines of the UE's player, in log order.
    qsizetype linesRead = 0;  ///< Lines of the log covered, including skipped ones.
};

/**
 * @brief Parses the `sim.log` lines of one UE.
 *
 * The log contains every simulated UE; lines of other UEs are skipped. `nodeId` is the ns-3
 * node of the UE (`ue-id`); the player id in the frame and pause lines is `nodeId - 5`.
 */
class SimLogParser {
public:
    /**
     * @brief Creates a parser for one UE.
     *
     * @param nodeId The ns-3 node of the UE.
     */
    explicit SimLogParser(int nodeId);

    /**
     * @brief Parses a piece of the log that starts and ends at line boundaries.
     *
     * Reads decision lines, `PLAYING FRAME` lines (buffer level and per-segment playback) and
     * `[MPEG_PLAYER]` pause and resume lines of this UE.
     *
     * @param chunk The piece of the log.
     * @return What the piece says about the UE.
     */
    [[nodiscard]] SimLogPart parseChunk(QByteArrayView chunk) const;

    /**
     * @brief Appends `part` to `accumulated`.
     *
     * Parts must be merged in log order; a segment whose frames are split between two parts is
     * joined.
     *
     * @param accumulated The parts merged so far; extended in place.
     * @param part The next part in log order.
     */
    static void merge(SimLogPart& accumulated, const SimLogPart& part);

    /**
     * @brief Builds the stalls from the pause and resume lines.
     *
     * Each pause ends at the first resume at or after it. A pause after the last frame of a
     * video that played to the end is the end of playback, not a stall; any other pause
     * without a resume is a terminal stall lasting until `stopTime`.
     *
     * @param log The merged log of the UE.
     * @param videoFinished True if the video played to the end.
     * @param stopTime Simulation stop time in seconds.
     * @return The stalls in pause order.
     */
    [[nodiscard]] static std::vector<Stall> stalls(const SimLogPart& log, bool videoFinished,
                                                   double stopTime);

    /**
     * @brief Returns the ns-3 node of the UE.
     *
     * @return The node id (`ue-id`).
     */
    [[nodiscard]] int nodeId() const { return nodeId_; }

    /**
     * @brief Returns the player id used in the frame and pause lines.
     *
     * @return `nodeId() - 5`.
     */
    [[nodiscard]] int playerId() const { return playerId_; }

private:
    int nodeId_;    ///< ns-3 node of the UE (`ue-id`)
    int playerId_;  ///< player id in the frame and pause lines: `nodeId_ - 5`
};

/**
 * @brief Returns the player id used in the frame and pause lines for an ns-3 node.
 *
 * @param nodeId The ns-3 node of the UE.
 * @return `nodeId - 5`.
 */
[[nodiscard]] constexpr int playerIdForNode(int nodeId)
{
    return nodeId - 5;
}

} // namespace abrviz
