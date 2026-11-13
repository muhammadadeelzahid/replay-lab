#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <span>
#include <vector>

namespace abrviz {

/// One rung of the bitrate ladder.
struct Rung {
    int index = 0;
    qint64 bitrate = 0;  ///< bits per second
    int width = 0;
    int height = 0;
};

/// `videos/<name>/video.json`.
struct VideoInfo {
    QString name;
    QString title;
    QString source;
    QString license;
    QString directory;  ///< absolute path of `videos/<name>`
    QString segmentFilePattern;  ///< e.g. `r{rung}/seg_{segment:04d}.mp4`
    double nominalSegmentDuration = 2.0;
    double frameRate = 0.0;
    int segmentCount = 0;
    std::vector<Rung> ladder;

    /// Absolute path of the media file for one segment at one rung.
    [[nodiscard]] QString segmentPath(int segment, int rung) const;
    [[nodiscard]] const Rung* rung(int index) const;
};

/// `run.json`, plus the bundle directory.
struct RunInfo {
    QString runId;
    QString bundleDirectory;
    QString scenario;
    QString policyName;
    QString policyClient;
    QString policyModel;
    QString notes;
    QString simLogSource;
    QString videoName;
    QString congestionControl;
    QString rlcMode;
    std::optional<qint64> feederLinkRate;  ///< bits per second; empty if uncongested
    int numUes = 1;
    int simulatedUes = 1;
    int numRelays = 0;
    int rngRun = 0;
    int nodeId = -1;                     ///< ns-3 node of the bundle's UE
    double playerSegmentDuration = 3.3;  ///< seconds of play per segment in the simulation
    double simStopTime = 0.0;
    double streamStart = 0.0;
    double logDecisionAgreement = 1.0;
};

/// A segment as it was downloaded and played (after the segment-shift correction).
struct Segment {
    int index = 0;
    double requestTime = 0.0;
    double doneTime = 0.0;
    int playedRung = 0;
    qint64 bytes = 0;
    QString mediaPath;
    double mediaDuration = 0.0;  ///< seconds, from the MP4 header (nominal if unreadable)
    bool played = true;          ///< false if the simulated player never showed it
};

/// One ABR decision: bundle record `record`, made at the end of download `record`, choosing
/// the rung of segment `record + 1`.
struct Decision {
    int record = 0;
    int forSegment = 0;
    double time = 0.0;
    int chosenRung = 0;
    std::optional<double> bufferLevel;
    std::optional<double> throughputEstimate;
    std::optional<double> rttMs;
    std::optional<double> lossRate;
    std::optional<double> decisionDelay;
    bool guardOverride = false;       ///< the buffer guard lowered the LLM's choice
    std::optional<int> llmRung;       ///< the rung the LLM wanted, when the guard overrode it
    std::optional<double> bufferFullWait;  ///< `del` in `sim.log`
    bool used = true;                 ///< false when `forSegment` is past the end of the video
};

/// `samples.jsonl`: network and player state at each decision.
struct Sample {
    double time = 0.0;
    std::optional<double> throughput;
    std::optional<double> rttMs;
    std::optional<double> lossRate;
    std::optional<double> bufferLevel;
};

/// Player buffer level from one `PLAYING FRAME` line.
struct BufferPoint {
    double time = 0.0;
    double level = 0.0;  ///< seconds
};

/// When a segment was on screen in the simulation, from the `PLAYING FRAME` lines.
struct SegmentPlayback {
    int segment = 0;
    double firstFrameTime = 0.0;
    double lastFrameTime = 0.0;
    int framesPlayed = 0;  ///< highest `FrameId` + 1 (100 when the segment played to the end)
    qint64 bitrate = 0;    ///< `Res` of the frames
};

struct Stall {
    double start = 0.0;
    double end = 0.0;
    bool terminal = false;  ///< playback never resumed

    [[nodiscard]] double duration() const { return end - start; }
};

/// `summary.json` as written by the exporter. Every field may be missing.
struct ExporterSummary {
    std::optional<double> averageBitrate;
    std::optional<double> averageThroughput;
    std::optional<int> switchCount;
    std::optional<bool> completed;
    std::optional<double> rebufferTime;
    std::optional<int> stallCount;
    std::optional<double> startupDelay;
};

struct Warning {
    enum class Kind {
        LogAgreement,
        PlaybackCutOff,
        SkippedSegments,
        RungMismatch,
        StallMismatch,
        MissingMedia,
    };
    Kind kind = Kind::LogAgreement;
    QString message;
};

/// Everything loaded from one bundle. Built once by the loader, then shared read-only as
/// `std::shared_ptr<const RunData>`.
struct RunData {
    RunInfo info;
    VideoInfo video;
    double streamStart = 0.0;
    double playbackStart = 0.0;
    double playbackEnd = 0.0;
    std::vector<Segment> segments;
    std::vector<Decision> decisions;
    std::vector<Sample> samples;
    std::vector<BufferPoint> buffer;
    std::vector<SegmentPlayback> playback;
    std::vector<Stall> stalls;
    ExporterSummary summary;
    std::vector<Warning> warnings;

    /// The segment on screen at `simTime`, or null before playback starts.
    [[nodiscard]] const SegmentPlayback* playbackAt(double simTime) const;
    [[nodiscard]] std::span<const Sample> samplesBetween(double from, double to) const;
    [[nodiscard]] std::span<const BufferPoint> bufferBetween(double from, double to) const;
    /// True if the simulation stopped before the last segment was fully played.
    [[nodiscard]] bool playbackCutOff() const;
    [[nodiscard]] bool hasWarning(Warning::Kind kind) const;
};

} // namespace abrviz
