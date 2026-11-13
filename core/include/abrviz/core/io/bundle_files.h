#pragma once

#include "abrviz/core/io/parsed.h"
#include "abrviz/core/model/run_data.h"

#include <QByteArrayView>

#include <optional>
#include <vector>

namespace abrviz {

/// One line of `segments.jsonl`, as written by the exporter (before the segment-shift
/// correction: `rung` is the rung chosen for the *next* segment).
struct SegmentRecord {
    int segment = 0;
    double requestTime = 0.0;
    double doneTime = 0.0;
    int rung = 0;
    qint64 bitrate = 0;
    qint64 bytes = 0;
    std::optional<double> bufferLevel;
    std::optional<double> throughputEstimate;
    std::optional<double> rttMs;
    std::optional<double> lossRate;
    std::optional<double> decisionDelay;
    bool fallback = false;
};

/// `player_events.jsonl`.
struct PlayerEvents {
    std::optional<double> streamStart;
    std::optional<double> playbackStart;
    std::optional<double> playbackEnd;
    std::vector<Stall> stalls;
};

/// `videos/<name>/segments.jsonl`: size of every (segment, rung).
class VideoSegmentTable {
public:
    VideoSegmentTable() = default;
    VideoSegmentTable(int segmentCount, int rungCount);

    void set(int segment, int rung, qint64 bytes);
    [[nodiscard]] std::optional<qint64> bytes(int segment, int rung) const;
    [[nodiscard]] int segmentCount() const { return segmentCount_; }
    [[nodiscard]] int rungCount() const { return rungCount_; }

private:
    [[nodiscard]] std::optional<std::size_t> slot(int segment, int rung) const;

    int segmentCount_ = 0;
    int rungCount_ = 0;
    std::vector<std::optional<qint64>> bytes_;
};

/// Parsers for the JSON files of a bundle. Each returns what it could read plus a list of
/// errors with file and line.
[[nodiscard]] Parsed<RunInfo> parseRunJson(QByteArrayView json, const QString& bundleDirectory);
[[nodiscard]] Parsed<std::vector<SegmentRecord>> parseSegmentRecords(QByteArrayView jsonl);
[[nodiscard]] Parsed<std::vector<Sample>> parseSamples(QByteArrayView jsonl);
[[nodiscard]] Parsed<PlayerEvents> parsePlayerEvents(QByteArrayView jsonl);
[[nodiscard]] Parsed<VideoInfo> parseVideoJson(QByteArrayView json, const QString& videoDirectory);
[[nodiscard]] Parsed<VideoSegmentTable> parseVideoSegments(QByteArrayView jsonl, int segmentCount,
                                                           int rungCount);
[[nodiscard]] Parsed<ExporterSummary> parseSummary(QByteArrayView json);

} // namespace abrviz
