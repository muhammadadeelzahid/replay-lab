#include "abrviz/core/load/bundle_reader.h"

#include "abrviz/core/io/mapped_file.h"
#include "abrviz/core/io/mp4_duration.h"
#include "abrviz/core/load/segment_shift.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

constexpr double kStallTolerance = 0.01;  // seconds

QString bundlePath(const QString& directory, const QString& relative)
{
    return QDir(directory).filePath(relative);
}

/// Maps a bundle file and hands its contents to `parse`. A missing or unreadable file becomes
/// an error in the result.
template <typename T, typename Parse>
Parsed<T> readMapped(const QString& directory, const QString& relative, Parse&& parse)
{
    QString error;
    const auto file = MappedFile::open(bundlePath(directory, relative), &error);
    if (!file) {
        Parsed<T> result;
        result.addError(relative, 0, QStringLiteral("cannot read: %1").arg(error));
        return result;
    }
    return parse(file->view());
}

void appendErrors(QStringList& to, const QStringList& from)
{
    to.append(from);
}

std::optional<Warning> stallWarning(const std::vector<Stall>& exported,
                                    const std::vector<Stall>& fromLog)
{
    bool same = exported.size() == fromLog.size();
    for (std::size_t i = 0; same && i < exported.size(); ++i)
        same = std::abs(exported[i].start - fromLog[i].start) < kStallTolerance;
    if (same)
        return std::nullopt;
    return Warning{Warning::Kind::StallMismatch,
                   QStringLiteral("player_events.jsonl has %1 stall(s), sim.log has %2")
                       .arg(exported.size())
                       .arg(fromLog.size())};
}

} // namespace

QString simLogPath(const QString& directory)
{
    return bundlePath(directory, QStringLiteral("logs/sim.log"));
}

QString policyLogPath(const QString& directory)
{
    return bundlePath(directory, QStringLiteral("logs/policy.log"));
}

Parsed<RunInfo> readRunInfo(const QString& directory)
{
    const QString absolute = QFileInfo(directory).absoluteFilePath();
    return readMapped<RunInfo>(directory, QStringLiteral("run.json"), [&](QByteArrayView text) {
        return parseRunJson(text, absolute);
    });
}

VideoParts readVideo(const QString& directory, const QString& videoName)
{
    VideoParts parts;
    const QString videoDirectory =
        QFileInfo(bundlePath(directory, QStringLiteral("videos/") + videoName)).absoluteFilePath();
    const QString prefix = QStringLiteral("videos/%1/").arg(videoName);
    parts.info = readMapped<VideoInfo>(directory, prefix + QStringLiteral("video.json"),
                                       [&](QByteArrayView text) {
                                           return parseVideoJson(text, videoDirectory);
                                       });
    const int rungs = static_cast<int>(parts.info.value.ladder.size());
    parts.sizes = readMapped<VideoSegmentTable>(
        directory, prefix + QStringLiteral("segments.jsonl"), [&](QByteArrayView text) {
            return parseVideoSegments(text, parts.info.value.segmentCount, rungs);
        });
    return parts;
}

Parsed<std::vector<SegmentRecord>> readSegmentRecords(const QString& directory)
{
    return readMapped<std::vector<SegmentRecord>>(directory, QStringLiteral("segments.jsonl"),
                                                  parseSegmentRecords);
}

Parsed<std::vector<Sample>> readSamples(const QString& directory)
{
    return readMapped<std::vector<Sample>>(directory, QStringLiteral("samples.jsonl"),
                                           parseSamples);
}

Parsed<PlayerEvents> readPlayerEvents(const QString& directory)
{
    return readMapped<PlayerEvents>(directory, QStringLiteral("player_events.jsonl"),
                                    parsePlayerEvents);
}

Parsed<ExporterSummary> readSummary(const QString& directory)
{
    return readMapped<ExporterSummary>(directory, QStringLiteral("summary.json"), parseSummary);
}

Parsed<std::vector<PolicyDecision>> readPolicyLog(const QString& directory, int nodeId)
{
    return readMapped<std::vector<PolicyDecision>>(
        directory, QStringLiteral("logs/policy.log"), [&](QByteArrayView text) {
            Parsed<std::vector<PolicyDecision>> result;
            result.value = parsePolicyLog(text, nodeId);
            return result;
        });
}

Parsed<SimLogPart> readSimLog(const QString& directory, int nodeId)
{
    return readMapped<SimLogPart>(directory, QStringLiteral("logs/sim.log"),
                                  [&](QByteArrayView text) {
                                      Parsed<SimLogPart> result;
                                      result.value = SimLogParser(nodeId).parseChunk(text);
                                      return result;
                                  });
}

QStringList playedMediaPaths(const std::vector<SegmentRecord>& records, const VideoInfo& video)
{
    QStringList paths;
    const auto count = std::min(records.size(),
                                static_cast<std::size_t>(std::max(video.segmentCount, 0)));
    for (std::size_t k = 0; k < count; ++k) {
        const int rung = k == 0 ? 0 : records[k - 1].rung;
        paths.append(video.segmentPath(static_cast<int>(k), rung));
    }
    return paths;
}

LoadResult assembleRunData(BundleParts parts)
{
    LoadResult result;
    QStringList& errors = result.errors;
    appendErrors(errors, parts.run.errors);
    appendErrors(errors, parts.video.info.errors);
    appendErrors(errors, parts.video.sizes.errors);
    appendErrors(errors, parts.records.errors);
    appendErrors(errors, parts.samples.errors);
    appendErrors(errors, parts.events.errors);
    appendErrors(errors, parts.summary.errors);
    appendErrors(errors, parts.policy.errors);
    appendErrors(errors, parts.simLog.errors);

    // Without these the run cannot be shown at all.
    if (!parts.run.ok() || !parts.video.info.ok() || parts.records.value.empty()
        || !parts.events.value.playbackStart)
        return result;

    auto run = std::make_shared<RunData>();
    run->info = std::move(parts.run.value);
    run->video = std::move(parts.video.info.value);
    run->samples = std::move(parts.samples.value);
    run->summary = parts.summary.value;
    run->streamStart = parts.events.value.streamStart.value_or(run->info.streamStart);
    run->playbackStart = *parts.events.value.playbackStart;
    run->playbackEnd = parts.events.value.playbackEnd.value_or(run->info.simStopTime);
    run->stalls = std::move(parts.events.value.stalls);

    auto shifted = applySegmentShift(parts.records.value, run->video, parts.video.sizes.value);
    run->segments = std::move(shifted.segments);
    run->decisions = std::move(shifted.decisions);

    // Real media durations, where the files could be read.
    int missingMedia = 0;
    for (std::size_t i = 0; i < run->segments.size(); ++i) {
        const auto duration = i < parts.mediaDurations.size() ? parts.mediaDurations[i]
                                                              : std::nullopt;
        if (duration)
            run->segments[i].mediaDuration = *duration;
        else
            ++missingMedia;
    }
    if (missingMedia > 0)
        run->warnings.push_back({Warning::Kind::MissingMedia,
                                 QStringLiteral("%1 of %2 media files are missing or unreadable; "
                                                "the replay is not available")
                                     .arg(missingMedia)
                                     .arg(run->segments.size())});

    // policy.log: line k (1-based) belongs to record k - 1.
    for (const PolicyDecision& p : parts.policy.value) {
        const int record = p.segmentOneBased - 1;
        if (record < 0 || static_cast<std::size_t>(record) >= run->decisions.size())
            continue;
        Decision& d = run->decisions[static_cast<std::size_t>(record)];
        d.llmRung = p.llmRung;
    }

    // sim.log: the decision after segment k is record k; frames give the playback timeline.
    SimLogPart& log = parts.simLog.value;
    for (const SimLogDecision& s : log.decisions) {
        if (s.segment >= 0 && static_cast<std::size_t>(s.segment) < run->decisions.size())
            run->decisions[static_cast<std::size_t>(s.segment)].bufferFullWait =
                s.bufferFullWait;
    }
    run->buffer = std::move(log.buffer);
    run->playback = std::move(log.playback);

    // Segments the simulated player never showed, before the last one it did show.
    if (!run->playback.empty()) {
        QStringList skipped;
        const int lastPlayed = run->playback.back().segment;
        for (Segment& segment : run->segments) {
            const bool seen = std::ranges::any_of(run->playback, [&](const SegmentPlayback& p) {
                return p.segment == segment.index;
            });
            segment.played = seen;
            if (!seen && segment.index < lastPlayed)
                skipped.append(QString::number(segment.index));
        }
        if (!skipped.isEmpty())
            run->warnings.push_back({Warning::Kind::SkippedSegments,
                                     QStringLiteral("The simulated player skipped segment(s) %1")
                                         .arg(skipped.join(QStringLiteral(", ")))});
    }

    // Checks against the log.
    int rungMismatches = 0;
    for (const SegmentPlayback& p : run->playback) {
        if (p.segment < 0 || static_cast<std::size_t>(p.segment) >= run->segments.size())
            continue;
        const Rung* rung =
            run->video.rung(run->segments[static_cast<std::size_t>(p.segment)].playedRung);
        if (!rung || rung->bitrate != p.bitrate)
            ++rungMismatches;
    }
    if (rungMismatches > 0)
        run->warnings.push_back({Warning::Kind::RungMismatch,
                                 QStringLiteral("%1 segment(s) were played at a different rung "
                                                "in sim.log")
                                     .arg(rungMismatches)});

    const bool videoFinished = !run->playback.empty()
                            && run->playback.back().segment + 1 == run->video.segmentCount
                            && run->playback.back().framesPlayed >= kFramesPerSegment;
    if (!run->playback.empty()) {
        if (auto warning = stallWarning(
                run->stalls, SimLogParser::stalls(log, videoFinished, run->playbackEnd)))
            run->warnings.push_back(*warning);
    }

    if (run->info.logDecisionAgreement < 1.0)
        run->warnings.push_back(
            {Warning::Kind::LogAgreement,
             QStringLiteral("sim.log comes from a re-run of this configuration: %1% of its "
                            "decisions match the bundle")
                 .arg(run->info.logDecisionAgreement * 100.0, 0, 'f', 1)});

    if (run->playbackCutOff()) {
        const SegmentPlayback& last = run->playback.back();
        run->warnings.push_back({Warning::Kind::PlaybackCutOff,
                                 QStringLiteral("The simulation stopped at %1 s while segment %2 "
                                                "of %3 was playing, after %4 of its 100 frames")
                                     .arg(run->playbackEnd, 0, 'f', 2)
                                     .arg(last.segment)
                                     .arg(run->video.segmentCount)
                                     .arg(last.framesPlayed)});
    }

    result.run = std::move(run);
    return result;
}

LoadResult loadBundleSync(const QString& directory)
{
    BundleParts parts;
    parts.directory = directory;
    parts.run = readRunInfo(directory);
    if (!parts.run.ok())
        return assembleRunData(std::move(parts));

    const RunInfo& info = parts.run.value;
    parts.video = readVideo(directory, info.videoName);
    parts.records = readSegmentRecords(directory);
    parts.samples = readSamples(directory);
    parts.events = readPlayerEvents(directory);
    parts.summary = readSummary(directory);
    parts.policy = readPolicyLog(directory, info.nodeId);
    parts.simLog = readSimLog(directory, info.nodeId);
    for (const QString& path : playedMediaPaths(parts.records.value, parts.video.info.value))
        parts.mediaDurations.push_back(readMp4Duration(path));
    return assembleRunData(std::move(parts));
}

} // namespace abrviz
