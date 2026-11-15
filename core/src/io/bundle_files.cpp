#include "abrviz/core/io/bundle_files.h"

#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/text_lines.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace abrviz {

namespace {

constexpr int kBundleVersion = 1;  ///< The only `bundle_version` of `run.json` that is supported.

/**
 * @brief Largest (segment, rung) table a video's `segments.jsonl` is read into.
 *
 * Far beyond any real video (hours of segments on dozens of rungs); a corrupt `segment_count`
 * must not make the reader allocate gigabytes.
 */
constexpr qint64 kMaxSegmentTableEntries = qint64(1) << 22;

/**
 * @brief Reads a JSON number.
 *
 * @param value The JSON value.
 * @return The number, or nothing if `value` is not a number.
 */
std::optional<double> optionalNumber(const QJsonValue& value)
{
    if (!value.isDouble())
        return std::nullopt;
    return value.toDouble();
}

/**
 * @brief Reads a JSON number as an int.
 *
 * @param value The JSON value.
 * @return The number, or nothing if `value` is not a number.
 */
std::optional<int> optionalInt(const QJsonValue& value)
{
    if (!value.isDouble())
        return std::nullopt;
    return value.toInt();
}

/**
 * @brief Reads a JSON number as a 64-bit integer.
 *
 * A value that is not an exact integer is truncated.
 *
 * @param value The JSON value.
 * @return The integer; 0 if `value` is not a number or does not fit in 64 bits.
 */
qint64 toInt64(const QJsonValue& value)
{
    return value.toInteger(doubleToInt64(value.toDouble()).value_or(0));
}

/**
 * @brief Parses one JSON object from a line or a whole file, without copying the bytes.
 *
 * @param text The JSON text; must stay valid until the call returns.
 * @param error Receives the parser message, or "expected a JSON object"; must not be null.
 * @return The object, or nothing if the text is not valid JSON or not an object.
 */
std::optional<QJsonObject> parseObject(QByteArrayView text, QString* error)
{
    QJsonParseError parseError{};
    const auto raw = QByteArray::fromRawData(text.data(), text.size());
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = parseError.errorString();
        return std::nullopt;
    }
    if (!document.isObject()) {
        *error = QStringLiteral("expected a JSON object");
        return std::nullopt;
    }
    return document.object();
}

/**
 * @brief Calls `fn(object, lineNumber)` for each non-empty line of a JSON Lines file.
 *
 * A line that is not a JSON object is reported in `result` and skipped.
 *
 * @param jsonl Contents of the file.
 * @param file File name used in error messages.
 * @param result Receives the parse errors.
 * @param fn Called with each parsed object and its one-based line number.
 */
template <typename T, typename Fn>
void forEachRecord(QByteArrayView jsonl, const QString& file, Parsed<T>& result, Fn&& fn)
{
    forEachLine(jsonl, [&](QByteArrayView line, qsizetype lineNumber) {
        if (line.trimmed().isEmpty())
            return;
        QString error;
        const auto object = parseObject(line, &error);
        if (!object) {
            result.addError(file, lineNumber, error);
            return;
        }
        fn(*object, lineNumber);
    });
}

/**
 * @brief Checks that a JSON object has all the given keys.
 *
 * @param object The object to check.
 * @param keys Keys that must be present.
 * @param file File name used in error messages.
 * @param line Line number used in error messages.
 * @param errors Receives one `missing field` message per missing key.
 * @return True if every key is present.
 */
bool requireKeys(const QJsonObject& object, std::initializer_list<const char*> keys,
                 const QString& file, qsizetype line, QStringList& errors)
{
    bool ok = true;
    for (const char* key : keys) {
        if (!object.contains(QLatin1StringView(key))) {
            errors.append(QStringLiteral("%1:%2: missing field \"%3\"")
                              .arg(file)
                              .arg(line)
                              .arg(QLatin1StringView(key)));
            ok = false;
        }
    }
    return ok;
}

} // namespace

// --- VideoSegmentTable ---------------------------------------------------------------------

VideoSegmentTable::VideoSegmentTable(int segmentCount, int rungCount)
    : segmentCount_(std::max(segmentCount, 0))
    , rungCount_(std::max(rungCount, 0))
    , bytes_(static_cast<std::size_t>(segmentCount_) * static_cast<std::size_t>(rungCount_))
{
}

std::optional<std::size_t> VideoSegmentTable::slot(int segment, int rung) const
{
    if (segment < 0 || segment >= segmentCount_ || rung < 0 || rung >= rungCount_)
        return std::nullopt;
    return static_cast<std::size_t>(segment) * static_cast<std::size_t>(rungCount_)
         + static_cast<std::size_t>(rung);
}

void VideoSegmentTable::set(int segment, int rung, qint64 bytes)
{
    if (const auto index = slot(segment, rung))
        bytes_[*index] = bytes;
}

std::optional<qint64> VideoSegmentTable::bytes(int segment, int rung) const
{
    const auto index = slot(segment, rung);
    return index ? bytes_[*index] : std::nullopt;
}

// --- run.json --------------------------------------------------------------------------------

Parsed<RunInfo> parseRunJson(QByteArrayView json, const QString& bundleDirectory)
{
    const QString file = QStringLiteral("run.json");
    Parsed<RunInfo> result;
    QString error;
    const auto root = parseObject(json, &error);
    if (!root) {
        result.addError(file, 0, error);
        return result;
    }
    if (root->value(QLatin1StringView("bundle_version")).toInt() != kBundleVersion) {
        result.addError(file, 0,
                        QStringLiteral("unsupported bundle_version %1 (expected %2)")
                            .arg(root->value(QLatin1StringView("bundle_version")).toInt())
                            .arg(kBundleVersion));
        return result;
    }

    RunInfo& info = result.value;
    info.bundleDirectory = bundleDirectory;
    info.runId = root->value(QLatin1StringView("run_id")).toString();
    info.scenario = root->value(QLatin1StringView("scenario")).toString();
    info.notes = root->value(QLatin1StringView("notes")).toString();
    info.simLogSource = root->value(QLatin1StringView("sim_log_source")).toString();
    info.simStopTime = root->value(QLatin1StringView("sim_stop_time_s")).toDouble();
    info.logDecisionAgreement =
        root->value(QLatin1StringView("sim_log_decision_agreement")).toDouble(1.0);

    const QJsonObject policy = root->value(QLatin1StringView("policy")).toObject();
    info.policyName = policy.value(QLatin1StringView("name")).toString();
    info.policyClient = policy.value(QLatin1StringView("client")).toString();
    info.policyModel = policy.value(QLatin1StringView("model")).toString();

    const QJsonObject network = root->value(QLatin1StringView("network")).toObject();
    info.numUes = network.value(QLatin1StringView("num_ues")).toInt(1);
    info.simulatedUes = network.value(QLatin1StringView("simulated_num_ues")).toInt(info.numUes);
    info.numRelays = network.value(QLatin1StringView("num_relays")).toInt();
    info.rngRun = network.value(QLatin1StringView("rng_run")).toInt();
    info.congestionControl = network.value(QLatin1StringView("congestion_control")).toString();
    info.rlcMode = network.value(QLatin1StringView("rlc_mode")).toString();
    if (const auto rate = optionalNumber(network.value(QLatin1StringView("feeder_link_rate_bps"))))
        info.feederLinkRate = doubleToInt64(*rate);

    const QJsonObject player = root->value(QLatin1StringView("player")).toObject();
    info.playerSegmentDuration =
        player.value(QLatin1StringView("segment_duration_s")).toDouble(3.3);

    const QJsonArray ues = root->value(QLatin1StringView("ues")).toArray();
    if (ues.isEmpty()) {
        result.addError(file, 0, QStringLiteral("no UE in \"ues\""));
        return result;
    }
    const QJsonObject ue = ues.first().toObject();
    info.nodeId = ue.value(QLatin1StringView("node_id")).toInt(-1);
    info.videoName = ue.value(QLatin1StringView("video")).toString();
    info.streamStart = ue.value(QLatin1StringView("stream_start_s")).toDouble();

    if (info.runId.isEmpty())
        result.addError(file, 0, QStringLiteral("missing run_id"));
    if (info.videoName.isEmpty())
        result.addError(file, 0, QStringLiteral("missing ues[0].video"));
    if (info.nodeId < 0)
        result.addError(file, 0, QStringLiteral("missing ues[0].node_id"));
    return result;
}

// --- segments.jsonl ------------------------------------------------------------------------

Parsed<std::vector<SegmentRecord>> parseSegmentRecords(QByteArrayView jsonl)
{
    const QString file = QStringLiteral("segments.jsonl");
    Parsed<std::vector<SegmentRecord>> result;
    forEachRecord(jsonl, file, result, [&](const QJsonObject& o, qsizetype line) {
        if (!requireKeys(o, {"segment", "request_s", "done_s", "rung", "bitrate_bps", "bytes"},
                         file, line, result.errors))
            return;
        SegmentRecord r;
        r.segment = o.value(QLatin1StringView("segment")).toInt();
        r.requestTime = o.value(QLatin1StringView("request_s")).toDouble();
        r.doneTime = o.value(QLatin1StringView("done_s")).toDouble();
        r.rung = o.value(QLatin1StringView("rung")).toInt();
        r.bitrate = toInt64(o.value(QLatin1StringView("bitrate_bps")));
        r.bytes = toInt64(o.value(QLatin1StringView("bytes")));

        const QJsonObject d = o.value(QLatin1StringView("decision")).toObject();
        r.bufferLevel = optionalNumber(d.value(QLatin1StringView("buffer_s")));
        r.throughputEstimate =
            optionalNumber(d.value(QLatin1StringView("throughput_estimate_bps")));
        r.rttMs = optionalNumber(d.value(QLatin1StringView("rtt_ms")));
        r.lossRate = optionalNumber(d.value(QLatin1StringView("loss_rate")));
        r.decisionDelay = optionalNumber(d.value(QLatin1StringView("decision_delay_s")));
        r.fallback = d.value(QLatin1StringView("fallback")).toBool();

        if (r.doneTime < r.requestTime)
            result.addError(file, line, QStringLiteral("done_s is before request_s"));
        if (!result.value.empty() && r.segment != result.value.back().segment + 1)
            result.addError(file, line, QStringLiteral("segment %1 does not follow %2")
                                            .arg(r.segment)
                                            .arg(result.value.back().segment));
        result.value.push_back(r);
    });
    if (result.value.empty() && result.ok())
        result.addError(file, 0, QStringLiteral("no segment records"));
    return result;
}

// --- samples.jsonl -------------------------------------------------------------------------

Parsed<std::vector<Sample>> parseSamples(QByteArrayView jsonl)
{
    const QString file = QStringLiteral("samples.jsonl");
    Parsed<std::vector<Sample>> result;
    forEachRecord(jsonl, file, result, [&](const QJsonObject& o, qsizetype line) {
        if (!requireKeys(o, {"t"}, file, line, result.errors))
            return;
        Sample s;
        s.time = o.value(QLatin1StringView("t")).toDouble();
        s.throughput = optionalNumber(o.value(QLatin1StringView("throughput_bps")));
        s.rttMs = optionalNumber(o.value(QLatin1StringView("rtt_ms")));
        s.lossRate = optionalNumber(o.value(QLatin1StringView("loss_rate")));
        s.bufferLevel = optionalNumber(o.value(QLatin1StringView("buffer_s")));
        if (!result.value.empty() && s.time < result.value.back().time)
            result.addError(file, line, QStringLiteral("time goes backwards"));
        result.value.push_back(s);
    });
    return result;
}

// --- player_events.jsonl -------------------------------------------------------------------

Parsed<PlayerEvents> parsePlayerEvents(QByteArrayView jsonl)
{
    const QString file = QStringLiteral("player_events.jsonl");
    Parsed<PlayerEvents> result;
    std::optional<Stall> open;
    forEachRecord(jsonl, file, result, [&](const QJsonObject& o, qsizetype line) {
        if (!requireKeys(o, {"t", "event"}, file, line, result.errors))
            return;
        const double t = o.value(QLatin1StringView("t")).toDouble();
        const QString event = o.value(QLatin1StringView("event")).toString();
        const bool terminal = o.value(QLatin1StringView("terminal")).toBool();
        if (event == QLatin1StringView("stream_start")) {
            result.value.streamStart = t;
        } else if (event == QLatin1StringView("playback_start")) {
            result.value.playbackStart = t;
        } else if (event == QLatin1StringView("playback_end")) {
            result.value.playbackEnd = t;
        } else if (event == QLatin1StringView("stall_start")) {
            if (open)
                result.addError(file, line, QStringLiteral("stall_start inside a stall"));
            open = Stall{t, t, terminal};
        } else if (event == QLatin1StringView("stall_end")) {
            if (!open) {
                result.addError(file, line, QStringLiteral("stall_end without stall_start"));
                return;
            }
            open->end = t;
            open->terminal = open->terminal || terminal;
            result.value.stalls.push_back(*open);
            open.reset();
        } else {
            result.addError(file, line, QStringLiteral("unknown event \"%1\"").arg(event));
        }
    });

    if (open) {
        // A stall that never ended lasts until playback ends.
        open->end = result.value.playbackEnd.value_or(open->start);
        open->terminal = true;
        result.value.stalls.push_back(*open);
    }
    if (!result.value.streamStart || !result.value.playbackStart || !result.value.playbackEnd)
        result.addError(file, 0,
                        QStringLiteral("needs stream_start, playback_start and playback_end"));
    return result;
}

// --- video.json ----------------------------------------------------------------------------

Parsed<VideoInfo> parseVideoJson(QByteArrayView json, const QString& videoDirectory)
{
    const QString file = QStringLiteral("video.json");
    Parsed<VideoInfo> result;
    QString error;
    const auto root = parseObject(json, &error);
    if (!root) {
        result.addError(file, 0, error);
        return result;
    }
    VideoInfo& v = result.value;
    v.directory = videoDirectory;
    v.name = root->value(QLatin1StringView("name")).toString();
    v.title = root->value(QLatin1StringView("title")).toString(v.name);
    v.source = root->value(QLatin1StringView("source")).toString();
    v.license = root->value(QLatin1StringView("license")).toString();
    v.nominalSegmentDuration =
        root->value(QLatin1StringView("segment_duration_s")).toDouble(2.0);
    v.frameRate = root->value(QLatin1StringView("frame_rate")).toDouble();
    v.segmentCount = root->value(QLatin1StringView("segment_count")).toInt();
    v.segmentFilePattern = root->value(QLatin1StringView("segment_file_pattern")).toString();

    const QJsonArray rungs = root->value(QLatin1StringView("rungs")).toArray();
    for (const QJsonValue& value : rungs) {
        const QJsonObject r = value.toObject();
        v.ladder.push_back(Rung{r.value(QLatin1StringView("rung")).toInt(),
                                toInt64(r.value(QLatin1StringView("bitrate_bps"))),
                                r.value(QLatin1StringView("width")).toInt(),
                                r.value(QLatin1StringView("height")).toInt()});
    }

    if (v.segmentCount <= 0)
        result.addError(file, 0, QStringLiteral("segment_count must be positive"));
    if (v.ladder.empty())
        result.addError(file, 0, QStringLiteral("no rungs"));
    for (std::size_t i = 0; i < v.ladder.size(); ++i) {
        if (v.ladder[i].index != static_cast<int>(i))
            result.addError(file, 0, QStringLiteral("rungs must be listed in order"));
    }
    if (!v.segmentFilePattern.isEmpty()
        && v.segmentFilePattern != QLatin1StringView("r{rung}/seg_{segment:04d}.mp4"))
        result.addError(file, 0, QStringLiteral("unsupported segment_file_pattern \"%1\"")
                                     .arg(v.segmentFilePattern));
    return result;
}

Parsed<VideoSegmentTable> parseVideoSegments(QByteArrayView jsonl, int segmentCount,
                                             int rungCount)
{
    const QString file = QStringLiteral("videos/segments.jsonl");
    Parsed<VideoSegmentTable> result;
    if (qint64(segmentCount) * qint64(rungCount) > kMaxSegmentTableEntries) {
        result.addError(file, 0, QStringLiteral("%1 segments at %2 rungs is too large a table")
                                     .arg(segmentCount)
                                     .arg(rungCount));
        return result;
    }
    result.value = VideoSegmentTable(segmentCount, rungCount);
    forEachRecord(jsonl, file, result, [&](const QJsonObject& o, qsizetype line) {
        if (!requireKeys(o, {"segment", "rung", "bytes"}, file, line, result.errors))
            return;
        result.value.set(o.value(QLatin1StringView("segment")).toInt(),
                         o.value(QLatin1StringView("rung")).toInt(),
                         toInt64(o.value(QLatin1StringView("bytes"))));
    });
    for (int s = 0; s < segmentCount; ++s) {
        for (int r = 0; r < rungCount; ++r) {
            if (!result.value.bytes(s, r))
                result.addError(file, 0,
                                QStringLiteral("no entry for segment %1, rung %2").arg(s).arg(r));
        }
    }
    return result;
}

// --- summary.json --------------------------------------------------------------------------

Parsed<ExporterSummary> parseSummary(QByteArrayView json)
{
    const QString file = QStringLiteral("summary.json");
    Parsed<ExporterSummary> result;
    QString error;
    const auto root = parseObject(json, &error);
    if (!root) {
        result.addError(file, 0, error);
        return result;
    }
    const QJsonArray ues = root->value(QLatin1StringView("ues")).toArray();
    if (ues.isEmpty())
        return result;
    const QJsonObject ue = ues.first().toObject();
    ExporterSummary& s = result.value;
    s.averageBitrate = optionalNumber(ue.value(QLatin1StringView("avg_bitrate_bps")));
    s.averageThroughput = optionalNumber(ue.value(QLatin1StringView("avg_throughput_bps")));
    s.switchCount = optionalInt(ue.value(QLatin1StringView("switch_count")));
    s.rebufferTime = optionalNumber(ue.value(QLatin1StringView("rebuffer_time_s")));
    s.stallCount = optionalInt(ue.value(QLatin1StringView("stall_count")));
    s.startupDelay = optionalNumber(ue.value(QLatin1StringView("startup_delay_s")));
    if (const QJsonValue completed = ue.value(QLatin1StringView("completed")); completed.isBool())
        s.completed = completed.toBool();
    return result;
}

} // namespace abrviz
