#pragma once

#include "abrviz/core/model/run_data.h"

#include <QByteArrayView>

#include <vector>

namespace abrviz {

/// A decision line: `<t> ue-id: N videoId: V segmentId: k newBitRate: X oldBitRate: Y ...`.
/// `segment` has just finished downloading at rate `oldBitRate`; `newBitRate` is the rate
/// chosen for segment `segment + 1`.
struct SimLogDecision {
    double time = 0.0;
    int segment = 0;
    qint64 newBitRate = 0;
    qint64 oldBitRate = 0;
    double throughputEstimate = 0.0;
    double interruptionTime = 0.0;
    double bufferLevel = 0.0;
    double bufferChange = 0.0;
    double bufferFullWait = 0.0;
    double fetchTime = 0.0;
    double lossRate = 0.0;
};

/// `[MPEG_PLAYER]` pause and resume lines.
struct PlayerPause {
    double time = 0.0;
};
struct PlayerResume {
    double time = 0.0;
    double pauseDuration = 0.0;
};

/// What `sim.log` says about one UE. Produced per chunk and merged in order.
struct SimLogPart {
    std::vector<SimLogDecision> decisions;
    std::vector<BufferPoint> buffer;
    std::vector<SegmentPlayback> playback;
    std::vector<PlayerPause> pauses;
    std::vector<PlayerResume> resumes;
    qsizetype linesRead = 0;
};

/// Parses the `sim.log` lines of one UE. The log contains every simulated UE; lines of other
/// UEs are skipped. `nodeId` is the ns-3 node of the UE (`ue-id`); the player id in the frame
/// and pause lines is `nodeId - 5`.
class SimLogParser {
public:
    explicit SimLogParser(int nodeId);

    /// Parses a piece of the log that starts and ends at line boundaries.
    [[nodiscard]] SimLogPart parseChunk(QByteArrayView chunk) const;

    /// Appends `part` to `accumulated`. Parts must be merged in log order; a segment whose
    /// frames are split between two parts is joined.
    static void merge(SimLogPart& accumulated, const SimLogPart& part);

    /// Stalls from the pause and resume lines. A pause after the last frame of a video that
    /// played to the end is the end of playback, not a stall; any other pause without a resume
    /// is a terminal stall lasting until `stopTime`.
    [[nodiscard]] static std::vector<Stall> stalls(const SimLogPart& log, bool videoFinished,
                                                   double stopTime);

    [[nodiscard]] int nodeId() const { return nodeId_; }
    [[nodiscard]] int playerId() const { return playerId_; }

private:
    int nodeId_;
    int playerId_;
};

/// Player id used in the frame and pause lines for an ns-3 node.
[[nodiscard]] constexpr int playerIdForNode(int nodeId)
{
    return nodeId - 5;
}

} // namespace abrviz
