#pragma once

/**
 * @file
 * @brief Readers for the files of a bundle, and assembly of the parts into a `RunData`.
 */

#include "abrviz/core/io/bundle_files.h"
#include "abrviz/core/io/policy_log_parser.h"
#include "abrviz/core/io/sim_log_parser.h"
#include "abrviz/core/load/load_result.h"

#include <optional>
#include <vector>

namespace abrviz {

/**
 * @brief Video description and per-segment sizes from `videos/<name>/`.
 */
struct VideoParts {
    Parsed<VideoInfo> info;  ///< `video.json`
    /** @brief `segments.jsonl` of the video: bytes of every (segment, rung). */
    Parsed<VideoSegmentTable> sizes;
};

/**
 * @brief Everything read from a bundle's files, before it is put together into a `RunData`.
 *
 * The synchronous reader and the parallel loader both fill this and call `assembleRunData`.
 */
struct BundleParts {
    QString directory;  ///< directory of the bundle, as given by the caller
    Parsed<RunInfo> run;                         ///< `run.json`
    VideoParts video;  ///< `videos/<name>/video.json` and `segments.jsonl`
    /** @brief `segments.jsonl`, before the segment-shift correction. */
    Parsed<std::vector<SegmentRecord>> records;
    Parsed<std::vector<Sample>> samples;         ///< `samples.jsonl`
    Parsed<PlayerEvents> events;                 ///< `player_events.jsonl`
    Parsed<ExporterSummary> summary;             ///< `summary.json`
    Parsed<std::vector<PolicyDecision>> policy;  ///< lines of `logs/policy.log` for the bundle's UE
    Parsed<SimLogPart> simLog;                   ///< what `logs/sim.log` says about the bundle's UE
    /// Duration of each played segment's media file, in segment order; empty entries for files
    /// that could not be read.
    std::vector<std::optional<double>> mediaDurations;
};

// Readers for the individual files of the bundle in `directory`. They report missing files as
// errors and never throw.

/**
 * @brief Reads `run.json`.
 *
 * Reports a missing or unreadable file as an error; never throws.
 *
 * @param directory Directory of the bundle.
 * @return The run information, with the absolute bundle directory, and its errors.
 */
[[nodiscard]] Parsed<RunInfo> readRunInfo(const QString& directory);

/**
 * @brief Reads `videos/<videoName>/video.json` and `videos/<videoName>/segments.jsonl`.
 *
 * Reports missing or unreadable files as errors; never throws.
 *
 * @param directory Directory of the bundle.
 * @param videoName Name of the video folder.
 * @return The video description and segment sizes, each with its errors.
 */
[[nodiscard]] VideoParts readVideo(const QString& directory, const QString& videoName);

/**
 * @brief Reads `segments.jsonl`.
 *
 * Reports a missing or unreadable file as an error; never throws.
 *
 * @param directory Directory of the bundle.
 * @return The segment records and their errors.
 */
[[nodiscard]] Parsed<std::vector<SegmentRecord>> readSegmentRecords(const QString& directory);

/**
 * @brief Reads `samples.jsonl`.
 *
 * Reports a missing or unreadable file as an error; never throws.
 *
 * @param directory Directory of the bundle.
 * @return The samples and their errors.
 */
[[nodiscard]] Parsed<std::vector<Sample>> readSamples(const QString& directory);

/**
 * @brief Reads `player_events.jsonl`.
 *
 * Reports a missing or unreadable file as an error; never throws.
 *
 * @param directory Directory of the bundle.
 * @return The player events and their errors.
 */
[[nodiscard]] Parsed<PlayerEvents> readPlayerEvents(const QString& directory);

/**
 * @brief Reads `summary.json`.
 *
 * Reports a missing or unreadable file as an error; never throws.
 *
 * @param directory Directory of the bundle.
 * @return The exporter summary and its errors.
 */
[[nodiscard]] Parsed<ExporterSummary> readSummary(const QString& directory);

/**
 * @brief Reads the lines of one UE from `logs/policy.log`.
 *
 * Reports a missing or unreadable file as an error; never throws. Malformed lines are skipped
 * without an error.
 *
 * @param directory Directory of the bundle.
 * @param nodeId The ns-3 node of the UE.
 * @return The decisions and the read error, if any.
 */
[[nodiscard]] Parsed<std::vector<PolicyDecision>> readPolicyLog(const QString& directory,
                                                                int nodeId);

/**
 * @brief Reads `logs/sim.log` in one piece.
 *
 * The parallel loader parses chunks instead. Reports a missing or unreadable file as an error;
 * never throws.
 *
 * @param directory Directory of the bundle.
 * @param nodeId The ns-3 node of the UE.
 * @return What the log says about the UE, and the read error, if any.
 */
[[nodiscard]] Parsed<SimLogPart> readSimLog(const QString& directory, int nodeId);

/**
 * @brief Returns the path of a bundle's `sim.log`.
 *
 * @param directory Directory of the bundle.
 * @return `directory/logs/sim.log`.
 */
[[nodiscard]] QString simLogPath(const QString& directory);

/**
 * @brief Returns the path of a bundle's `policy.log`.
 *
 * @param directory Directory of the bundle.
 * @return `directory/logs/policy.log`.
 */
[[nodiscard]] QString policyLogPath(const QString& directory);

/**
 * @brief Returns the media files of the played segments, in order.
 *
 * Needs the records and the video description. Segment 0 uses rung 0 and segment `k` the rung
 * of record `k - 1` (the segment-shift correction).
 *
 * @param records The records of `segments.jsonl`.
 * @param video The video description.
 * @return One path per record, up to the video's segment count.
 */
[[nodiscard]] QStringList playedMediaPaths(const std::vector<SegmentRecord>& records,
                                           const VideoInfo& video);

/**
 * @brief Builds the `RunData` from the parts.
 *
 * Applies the segment-shift correction, joins the logs to the segments and decisions, and adds
 * warnings for whatever does not agree. Also reads the network topology from `sim.log`.
 *
 * @param parts Everything read from the bundle; consumed.
 * @return The run and every error of the parts. `run` is null if `run.json` or `video.json`
 *         has errors, there are no segment records, or there is no playback start.
 */
[[nodiscard]] LoadResult assembleRunData(BundleParts parts);

/**
 * @brief Reads and assembles a bundle on the calling thread.
 *
 * Stops after `run.json` if that file has errors.
 *
 * @param directory Directory of the bundle.
 * @return The result of the load.
 * @see loadBundle()
 */
[[nodiscard]] LoadResult loadBundleSync(const QString& directory);

} // namespace abrviz
