#pragma once

/**
 * @file
 * @brief The data model of one loaded run: video, segments, decisions, logs and warnings.
 */

#include <QString>
#include <QStringList>

#include <optional>
#include <span>
#include <vector>

namespace abrviz {

/** @brief Frames in every segment of the simulated player (`FrameId` 0 to 99). */
inline constexpr int kFramesPerSegment = 100;

/**
 * @brief One rung of the bitrate ladder.
 */
struct Rung {
    /** @brief Zero-based rung index (`rung` in `video.json`); equals the position in the ladder. */
    int index = 0;
    qint64 bitrate = 0;  ///< bits per second
    int width = 0;   ///< frame width in pixels (`width`)
    int height = 0;  ///< frame height in pixels (`height`)
};

/**
 * @brief Contents of `videos/<name>/video.json`.
 */
struct VideoInfo {
    QString name;     ///< short name of the video (`name`)
    QString title;    ///< display title (`title`); `name` if missing
    QString source;   ///< where the footage comes from (`source`); empty if missing
    QString license;  ///< licence of the footage (`license`); empty if missing
    QString directory;  ///< absolute path of `videos/<name>`
    QString segmentFilePattern;  ///< e.g. `r{rung}/seg_{segment:04d}.mp4`
    double nominalSegmentDuration = 2.0;  ///< seconds
    double frameRate = 0.0;    ///< frames per second (`frame_rate`); 0 if missing
    int segmentCount = 0;      ///< number of segments (`segment_count`); must be positive
    std::vector<Rung> ladder;  ///< the rungs (`rungs`); entry i has index i

    /**
     * @brief Returns the absolute path of the media file for one segment at one rung.
     *
     * Always uses the pattern `r{rung}/seg_{segment:04d}.mp4`; other patterns are not
     * supported.
     *
     * @param segment Zero-based segment index.
     * @param rung Zero-based rung index.
     * @return The path inside `directory`. The file is not checked for existence.
     */
    [[nodiscard]] QString segmentPath(int segment, int rung) const;

    /**
     * @brief Returns a rung of the ladder.
     *
     * @param index Zero-based rung index.
     * @return The rung, or null if `index` is out of range.
     */
    [[nodiscard]] const Rung* rung(int index) const;
};

/**
 * @brief Contents of `run.json`, plus the bundle directory.
 */
struct RunInfo {
    QString runId;              ///< run identifier (`run_id`); required
    QString bundleDirectory;    ///< absolute path of the bundle directory
    QString scenario;           ///< scenario name (`scenario`); empty if missing
    QString policyName;         ///< ABR policy name (`policy.name`)
    QString policyClient;       ///< policy client (`policy.client`)
    QString policyModel;        ///< model behind the policy (`policy.model`)
    QString notes;              ///< free text from the exporter (`notes`)
    QString simLogSource;  ///< origin of `sim.log` as stated by the exporter (`sim_log_source`)
    QString videoName;  ///< video streamed by the UE (`ues[0].video`), the folder under `videos/`
    QString congestionControl;  ///< TCP congestion control (`network.congestion_control`)
    QString rlcMode;            ///< RLC mode (`network.rlc_mode`)
    std::optional<qint64> feederLinkRate;  ///< bits per second; empty if uncongested
    int numUes = 1;     ///< UEs in this bundle (`network.num_ues`); 1 if missing
    /** @brief UEs sharing the feeder (`network.simulated_num_ues`); `numUes` if missing. */
    int simulatedUes = 1;
    int numRelays = 0;  ///< IAB relays (`network.num_relays`); 0 if missing
    int rngRun = 0;     ///< ns-3 RngRun value, shown as the seed (`network.rng_run`)
    int nodeId = -1;                     ///< ns-3 node of the bundle's UE
    double playerSegmentDuration = 3.3;  ///< seconds of play per segment in the simulation
    double simStopTime = 0.0;  ///< simulation stop time in seconds (`sim_stop_time_s`)
    double streamStart = 0.0;  ///< seconds; when the UE starts streaming (`ues[0].stream_start_s`)
    double logDecisionAgreement = 1.0;   ///< fraction of `sim.log` decisions matching the bundle
};

/**
 * @brief A segment as it was downloaded and played (after the segment-shift correction).
 */
struct Segment {
    int index = 0;             ///< zero-based segment index
    double requestTime = 0.0;  ///< seconds; download request (`request_s` of record `index`)
    double doneTime = 0.0;     ///< seconds; download end (`done_s` of record `index`)
    int playedRung = 0;  ///< rung played: 0 for segment 0, else the rung of record `index - 1`
    /** @brief Media file size at `playedRung`, from `videos/<name>/segments.jsonl`; 0 if absent. */
    qint64 bytes = 0;
    QString mediaPath;         ///< absolute path of the media file at `playedRung`
    double mediaDuration = 0.0;  ///< seconds, from the MP4 header (nominal if unreadable)
    bool played = true;          ///< false if the simulated player never showed it
};

/**
 * @brief One ABR decision.
 *
 * Bundle record `record`, made at the end of download `record`, choosing the rung of segment
 * `record + 1`.
 */
struct Decision {
    int record = 0;      ///< zero-based index of the `segments.jsonl` record
    int forSegment = 0;  ///< segment whose rung this decision sets: `record + 1`
    double time = 0.0;   ///< seconds; end of download `record` (`done_s`)
    int chosenRung = 0;  ///< rung chosen for `forSegment` (`rung` of the record)
    /** @brief Seconds of buffer the policy saw (`decision.buffer_s`); empty if missing. */
    std::optional<double> bufferLevel;
    /** @brief Bits per second (`decision.throughput_estimate_bps`); empty if missing. */
    std::optional<double> throughputEstimate;
    /** @brief Round-trip time in milliseconds (`decision.rtt_ms`); empty if missing. */
    std::optional<double> rttMs;
    /** @brief Loss rate as a fraction (`decision.loss_rate`); empty if missing. */
    std::optional<double> lossRate;
    /** @brief Seconds the policy took (`decision.decision_delay_s`); empty if missing. */
    std::optional<double> decisionDelay;
    bool guardOverride = false;       ///< the buffer guard lowered the LLM's choice
    std::optional<int> llmRung;       ///< the rung the LLM wanted, when the guard overrode it
    std::optional<double> bufferFullWait;  ///< `del` in `sim.log`
    bool used = true;                 ///< false when `forSegment` is past the end of the video
};

/**
 * @brief One line of `samples.jsonl`: network and player state at each decision.
 */
struct Sample {
    double time = 0.0;                  ///< simulation time in seconds (`t`)
    std::optional<double> throughput;   ///< bits per second (`throughput_bps`); empty if missing
    std::optional<double> rttMs;  ///< round-trip time in milliseconds (`rtt_ms`); empty if missing
    std::optional<double> lossRate;     ///< loss rate as a fraction (`loss_rate`); empty if missing
    std::optional<double> bufferLevel;  ///< player buffer in seconds (`buffer_s`); empty if missing
};

/**
 * @brief Player buffer level from one `PLAYING FRAME` line.
 */
struct BufferPoint {
    double time = 0.0;  ///< simulation time of the frame, in seconds
    double level = 0.0;  ///< seconds
};

/**
 * @brief When a segment was on screen in the simulation, from the `PLAYING FRAME` lines.
 */
struct SegmentPlayback {
    int segment = 0;              ///< zero-based segment index (`SegId`)
    double firstFrameTime = 0.0;  ///< simulation time of its first `PLAYING FRAME` line, in seconds
    double lastFrameTime = 0.0;   ///< simulation time of its last `PLAYING FRAME` line, in seconds
    int framesPlayed = 0;  ///< highest `FrameId` + 1 (100 when the segment played to the end)
    qint64 bitrate = 0;    ///< `Res` of the frames
};

/**
 * @brief A period during which playback was paused waiting for data.
 */
struct Stall {
    double start = 0.0;  ///< simulation time the stall began, in seconds
    double end = 0.0;    ///< simulation time playback resumed (or the run ended), in seconds
    bool terminal = false;  ///< playback never resumed

    /**
     * @brief Returns the length of the stall.
     *
     * @return `end - start`, in seconds.
     */
    [[nodiscard]] double duration() const { return end - start; }
};

/**
 * @brief Contents of `summary.json` as written by the exporter.
 *
 * Every field may be missing.
 */
struct ExporterSummary {
    /** @brief Bits per second, mean of the chosen rungs (`avg_bitrate_bps`). */
    std::optional<double> averageBitrate;
    std::optional<double> averageThroughput;  ///< bits per second (`avg_throughput_bps`)
    std::optional<int> switchCount;           ///< changes of chosen rung (`switch_count`)
    /** @brief Every segment was downloaded (`completed`); not that every one was played. */
    std::optional<bool> completed;
    std::optional<double> rebufferTime;       ///< total stall time in seconds (`rebuffer_time_s`)
    std::optional<int> stallCount;            ///< number of stalls (`stall_count`)
    /** @brief Seconds from stream start to playback start (`startup_delay_s`). */
    std::optional<double> startupDelay;
};

/**
 * @brief A problem found while assembling a run, shown to the user.
 */
struct Warning {
    /**
     * @brief What the warning is about.
     */
    enum class Kind {
        LogAgreement,     ///< `sim.log` comes from a re-run with different decisions.
        PlaybackCutOff,   ///< The simulation stopped before the last segment was played.
        SkippedSegments,  ///< The simulated player never showed some segments.
        RungMismatch,     ///< `sim.log` played some segments at a different rung.
        StallMismatch,    ///< `player_events.jsonl` and `sim.log` disagree on the stalls.
        MissingMedia,     ///< Some media files are missing or unreadable.
    };
    Kind kind = Kind::LogAgreement;  ///< what the warning is about
    QString message;                 ///< text shown to the user
};

/**
 * @brief The simulated network, from the set-up lines of `sim.log`.
 *
 * A satellite gNB (the donor), IAB relays on the ground, and the UEs, which attach to the
 * closest cell.
 */
struct NetworkTopology {
    std::optional<int> satelliteNode;  ///< ns-3 node of the satellite gNB; empty if not in the log
    std::optional<double> satelliteAltitude;   ///< metres above the ground plane
    int relayCount = 0;  ///< number of `Install IAB device` lines
    std::optional<double> backhaulDistance;    ///< metres from the relay to the satellite
    /** @brief Carrier centre frequency in GHz; empty if not in the log. */
    std::optional<double> carrierFrequencyGHz;
    std::optional<double> bandwidthMHz;  ///< total bandwidth in MHz; empty if not in the log
    std::optional<int> servingNode;            ///< the cell the bundle's UE attached to
    std::optional<double> accessDistance;      ///< metres from the UE to that cell
    bool viaRelay = false;                     ///< the UE is served by an IAB relay

    /**
     * @brief Tells whether the set-up lines described a satellite.
     *
     * @return True if the satellite altitude is known.
     */
    [[nodiscard]] bool hasSatellite() const { return satelliteAltitude.has_value(); }
};

/**
 * @brief Everything loaded from one bundle.
 *
 * Built once by the loader, then shared read-only as `std::shared_ptr<const RunData>`.
 */
struct RunData {
    RunInfo info;                     ///< contents of `run.json`
    NetworkTopology topology;         ///< network set-up from the head of `sim.log`
    VideoInfo video;                  ///< contents of `video.json`
    double streamStart = 0.0;  ///< seconds; `stream_start` event, else `RunInfo::streamStart`
    double playbackStart = 0.0;       ///< seconds; `playback_start` event in `player_events.jsonl`
    double playbackEnd = 0.0;  ///< seconds; `playback_end` event, else `RunInfo::simStopTime`
    /** @brief Downloaded segments in index order, after the segment-shift correction. */
    std::vector<Segment> segments;
    std::vector<Decision> decisions;  ///< one per `segments.jsonl` record, in record order
    std::vector<Sample> samples;          ///< expected in time order
    std::vector<BufferPoint> buffer;      ///< expected in time order
    std::vector<SegmentPlayback> playback;  ///< expected in order of first frame time
    std::vector<Stall> stalls;      ///< stalls from `player_events.jsonl`, in time order
    ExporterSummary summary;        ///< contents of `summary.json`
    std::vector<Warning> warnings;  ///< problems found while assembling the run

    /**
     * @brief Returns the segment on screen at a simulation time.
     *
     * This is the last playback entry whose first frame is at or before `simTime`.
     *
     * @param simTime Simulation time in seconds.
     * @return The playback entry, or null before playback starts.
     */
    [[nodiscard]] const SegmentPlayback* playbackAt(double simTime) const;

    /**
     * @brief Returns the samples with a time in `[from, to]`.
     *
     * @param from Start time in seconds.
     * @param to End time in seconds.
     * @return A view into `samples`; empty if none fall in the range.
     */
    [[nodiscard]] std::span<const Sample> samplesBetween(double from, double to) const;

    /**
     * @brief Returns the buffer points with a time in `[from, to]`.
     *
     * @param from Start time in seconds.
     * @param to End time in seconds.
     * @return A view into `buffer`; empty if none fall in the range.
     */
    [[nodiscard]] std::span<const BufferPoint> bufferBetween(double from, double to) const;

    /**
     * @brief Tells whether the simulation stopped before the last segment was fully played.
     *
     * @return True if the last played segment is not the video's last segment or did not show
     *         all its frames; false if nothing was played.
     */
    [[nodiscard]] bool playbackCutOff() const;

    /**
     * @brief Tells whether a warning of the given kind was raised.
     *
     * @param kind The kind to look for.
     * @return True if `warnings` contains one of that kind.
     */
    [[nodiscard]] bool hasWarning(Warning::Kind kind) const;
};

} // namespace abrviz
