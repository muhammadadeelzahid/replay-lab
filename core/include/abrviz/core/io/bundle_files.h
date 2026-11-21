#pragma once

/**
 * @file
 * @brief Record types and parsers for the JSON and JSON Lines files of a run bundle.
 */

#include "abrviz/core/io/parsed.h"
#include "abrviz/core/model/run_data.h"

#include <QByteArrayView>

#include <optional>
#include <vector>

namespace abrviz {

/**
 * @brief One line of `segments.jsonl`, as written by the exporter.
 *
 * The record is taken before the segment-shift correction: `rung` is the rung chosen for the
 * *next* segment, not for this one.
 *
 * @see applySegmentShift()
 */
struct SegmentRecord {
    int segment = 0;                            ///< Zero-based segment index.
    double requestTime = 0.0;                   ///< Request time in seconds (`request_s`).
    double doneTime = 0.0;                      ///< Download end time in seconds (`done_s`).
    int rung = 0;                               ///< Rung chosen for the next segment.
    qint64 bitrate = 0;                         ///< Bitrate of `rung` in bits per second.
    /** @brief Size in bytes (`bytes`); the exporter looked it up with the shifted `rung`. */
    qint64 bytes = 0;
    std::optional<double> bufferLevel;          ///< Buffer level at the decision, in seconds.
    std::optional<double> throughputEstimate;   ///< Policy throughput estimate in bits/s.
    std::optional<double> rttMs;                ///< Round-trip time in milliseconds.
    std::optional<double> lossRate;             ///< Loss rate as a fraction.
    std::optional<double> decisionDelay;        ///< `decision_delay_s`, in seconds.
    bool fallback = false;                      ///< Set when the buffer guard overrode the LLM.
};

/**
 * @brief Contents of `player_events.jsonl`: playback milestones and stalls.
 */
struct PlayerEvents {
    std::optional<double> streamStart;    ///< `stream_start` time in seconds.
    std::optional<double> playbackStart;  ///< `playback_start` time in seconds.
    std::optional<double> playbackEnd;    ///< `playback_end` time in seconds.
    /**
     * @brief Stalls paired from `stall_start` and `stall_end`, in order; an unended one is
     *       terminal.
     */
    std::vector<Stall> stalls;
};

/**
 * @brief Size of every (segment, rung) pair, read from `videos/<name>/segments.jsonl`.
 *
 * Stored as a dense table; entries that were not in the file are empty.
 */
class VideoSegmentTable {
public:
    /**
     * @brief Creates an empty table with no segments and no rungs.
     */
    VideoSegmentTable() = default;

    /**
     * @brief Creates a table of the given dimensions with every entry empty.
     *
     * Negative dimensions are treated as 0.
     *
     * @param segmentCount Number of segments in the video.
     * @param rungCount Number of rungs in the ladder.
     */
    VideoSegmentTable(int segmentCount, int rungCount);

    /**
     * @brief Stores the size of one segment at one rung.
     *
     * Indices outside the table are ignored.
     *
     * @param segment Zero-based segment index.
     * @param rung Zero-based rung index.
     * @param bytes Size of the segment file in bytes.
     */
    void set(int segment, int rung, qint64 bytes);

    /**
     * @brief Returns the size of one segment at one rung.
     *
     * @param segment Zero-based segment index.
     * @param rung Zero-based rung index.
     * @return The size in bytes, or nothing if the indices are out of range or no size was set.
     */
    [[nodiscard]] std::optional<qint64> bytes(int segment, int rung) const;

    /**
     * @brief Stores the picture quality of one segment at one rung.
     *
     * Indices outside the table are ignored.
     *
     * @param segment Zero-based segment index.
     * @param rung Zero-based rung index.
     * @param quality The scores read from the file.
     */
    void setQuality(int segment, int rung, const SegmentQuality& quality);

    /**
     * @brief Returns the picture quality of one segment at one rung.
     *
     * @param segment Zero-based segment index.
     * @param rung Zero-based rung index.
     * @return The scores, or null if the indices are out of range or the file had none.
     */
    [[nodiscard]] const SegmentQuality* quality(int segment, int rung) const;

    /**
     * @brief Returns every entry's quality, row-major by segment.
     *
     * @return The scores; empty if the file had none.
     */
    [[nodiscard]] const std::vector<SegmentQuality>& qualities() const { return quality_; }

    /**
     * @brief Returns the number of segments the table was created with.
     *
     * @return The segment count.
     */
    [[nodiscard]] int segmentCount() const { return segmentCount_; }

    /**
     * @brief Returns the number of rungs the table was created with.
     *
     * @return The rung count.
     */
    [[nodiscard]] int rungCount() const { return rungCount_; }

private:
    /**
     * @brief Maps a (segment, rung) pair to its index in `bytes_`.
     *
     * @param segment Zero-based segment index.
     * @param rung Zero-based rung index.
     * @return The index, or nothing if either index is out of range.
     */
    [[nodiscard]] std::optional<std::size_t> slot(int segment, int rung) const;

    int segmentCount_ = 0;  ///< Number of segments (rows).
    int rungCount_ = 0;     ///< Number of rungs (columns).
    std::vector<std::optional<qint64>> bytes_;  ///< Row-major: segment * rungCount_ + rung.
    std::vector<SegmentQuality> quality_;  ///< Same layout as `bytes_`; empty if none was read.
};

/**
 * @brief Parses `run.json`.
 *
 * Reads the run id, scenario, policy, network, player settings and the first entry of `ues`.
 * Parsing stops early if the JSON is invalid, `bundle_version` is not supported or `ues` is
 * empty.
 *
 * @param json Contents of `run.json`.
 * @param bundleDirectory Directory of the bundle, stored in the result.
 * @return The run information read, with an error for each problem found (missing `run_id`,
 *         `ues[0].video` or `ues[0].node_id` included).
 */
[[nodiscard]] Parsed<RunInfo> parseRunJson(QByteArrayView json, const QString& bundleDirectory);

/**
 * @brief Parses `segments.jsonl`.
 *
 * Records missing a required field are skipped. A record whose `done_s` is before its
 * `request_s`, or whose segment does not follow the previous one, is kept but reported.
 *
 * @param jsonl Contents of `segments.jsonl`.
 * @return The records in file order, with an error per problem with file and line; an error is
 *         also added if the file has no records.
 */
[[nodiscard]] Parsed<std::vector<SegmentRecord>> parseSegmentRecords(QByteArrayView jsonl);

/**
 * @brief Parses `samples.jsonl`.
 *
 * Records without `t` are skipped. A sample earlier than the previous one is kept but reported.
 *
 * @param jsonl Contents of `samples.jsonl`.
 * @return The samples in file order, with an error per problem with file and line.
 */
[[nodiscard]] Parsed<std::vector<Sample>> parseSamples(QByteArrayView jsonl);

/**
 * @brief Parses `player_events.jsonl`.
 *
 * Pairs `stall_start` and `stall_end` events into stalls. A stall that never ends is closed at
 * `playback_end` (or its own start if there is none) and marked terminal.
 *
 * @param jsonl Contents of `player_events.jsonl`.
 * @return The events read, with an error per unknown event, unpaired stall event, or missing
 *         `stream_start`, `playback_start` or `playback_end`.
 */
[[nodiscard]] Parsed<PlayerEvents> parsePlayerEvents(QByteArrayView jsonl);

/**
 * @brief Parses a video's `video.json`.
 *
 * @param json Contents of `video.json`.
 * @param videoDirectory Directory of the video, stored in the result.
 * @return The video information, with an error if `segment_count` is not positive, there are
 *         no rungs, the rungs are not listed in order, or `segment_file_pattern` is not the one
 *         supported pattern.
 */
[[nodiscard]] Parsed<VideoInfo> parseVideoJson(QByteArrayView json, const QString& videoDirectory);

/**
 * @brief Parses a video's `segments.jsonl` into a table of segment sizes.
 *
 * @param jsonl Contents of `videos/<name>/segments.jsonl`.
 * @param segmentCount Number of segments of the video.
 * @param rungCount Number of rungs of the video.
 * @return The table, with an error for every (segment, rung) pair that has no entry; an empty
 *         table with a single error if the dimensions are far beyond any real video.
 */
[[nodiscard]] Parsed<VideoSegmentTable> parseVideoSegments(QByteArrayView jsonl, int segmentCount,
                                                           int rungCount);

/**
 * @brief Parses the exporter's `summary.json`.
 *
 * Only the first entry of `ues` is read; fields that are missing stay empty.
 *
 * @param json Contents of `summary.json`.
 * @return The summary; an empty summary if `ues` is empty, with an error only if the JSON is
 *         invalid.
 */
[[nodiscard]] Parsed<ExporterSummary> parseSummary(QByteArrayView json);

} // namespace abrviz
