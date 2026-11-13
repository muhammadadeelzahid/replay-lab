#pragma once

#include "abrviz/core/io/bundle_files.h"
#include "abrviz/core/io/policy_log_parser.h"
#include "abrviz/core/io/sim_log_parser.h"
#include "abrviz/core/load/load_result.h"

#include <optional>
#include <vector>

namespace abrviz {

/// Video description and per-segment sizes from `videos/<name>/`.
struct VideoParts {
    Parsed<VideoInfo> info;
    Parsed<VideoSegmentTable> sizes;
};

/// Everything read from a bundle's files, before it is put together into a `RunData`.
/// The synchronous reader and the parallel loader both fill this and call `assembleRunData`.
struct BundleParts {
    QString directory;
    Parsed<RunInfo> run;
    VideoParts video;
    Parsed<std::vector<SegmentRecord>> records;
    Parsed<std::vector<Sample>> samples;
    Parsed<PlayerEvents> events;
    Parsed<ExporterSummary> summary;
    Parsed<std::vector<PolicyDecision>> policy;
    Parsed<SimLogPart> simLog;
    /// Duration of each played segment's media file, in segment order; empty entries for files
    /// that could not be read.
    std::vector<std::optional<double>> mediaDurations;
};

// Readers for the individual files of the bundle in `directory`. They report missing files as
// errors and never throw.
[[nodiscard]] Parsed<RunInfo> readRunInfo(const QString& directory);
[[nodiscard]] VideoParts readVideo(const QString& directory, const QString& videoName);
[[nodiscard]] Parsed<std::vector<SegmentRecord>> readSegmentRecords(const QString& directory);
[[nodiscard]] Parsed<std::vector<Sample>> readSamples(const QString& directory);
[[nodiscard]] Parsed<PlayerEvents> readPlayerEvents(const QString& directory);
[[nodiscard]] Parsed<ExporterSummary> readSummary(const QString& directory);
[[nodiscard]] Parsed<std::vector<PolicyDecision>> readPolicyLog(const QString& directory,
                                                                int nodeId);
/// Reads `logs/sim.log` in one piece. The parallel loader parses chunks instead.
[[nodiscard]] Parsed<SimLogPart> readSimLog(const QString& directory, int nodeId);

[[nodiscard]] QString simLogPath(const QString& directory);
[[nodiscard]] QString policyLogPath(const QString& directory);

/// Media files of the played segments, in order. Needs the records and the video description.
[[nodiscard]] QStringList playedMediaPaths(const std::vector<SegmentRecord>& records,
                                           const VideoInfo& video);

/// Builds the `RunData` from the parts: applies the segment-shift correction, joins the logs
/// to the segments and decisions, and adds warnings for whatever does not agree.
[[nodiscard]] LoadResult assembleRunData(BundleParts parts);

/// Reads and assembles a bundle on the calling thread.
[[nodiscard]] LoadResult loadBundleSync(const QString& directory);

} // namespace abrviz
