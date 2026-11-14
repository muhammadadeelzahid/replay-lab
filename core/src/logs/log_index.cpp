#include "abrviz/core/logs/log_index.h"

#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/sim_log_parser.h"
#include "abrviz/core/io/text_lines.h"

#include <QPromise>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <set>

namespace abrviz {

namespace {

/// The number in a `key=<number>` word, ignoring trailing `s` and `.` (`t=4.84331s.`).
std::optional<double> assignment(const Tokens& tokens, QByteArrayView key)
{
    for (const QByteArrayView token : tokens.all()) {
        if (!token.startsWith(key))
            continue;
        QByteArrayView value = token.sliced(key.size());
        while (!value.isEmpty() && (value.endsWith('.') || value.endsWith('s')))
            value.chop(1);
        return toDouble(value);
    }
    return std::nullopt;
}

qint16 toUe(std::optional<int> node)
{
    return node ? static_cast<qint16>(*node) : qint16(-1);
}

} // namespace

LogLevel levelOf(LogKind kind)
{
    switch (kind) {
    case LogKind::Progress:
    case LogKind::Frame:
        return LogLevel::Debug;
    case LogKind::Pause:
    case LogKind::GuardOverride:
        return LogLevel::Warning;
    default:
        return LogLevel::Info;
    }
}

QString kindName(LogKind kind)
{
    switch (kind) {
    case LogKind::Setup: return QStringLiteral("setup");
    case LogKind::Progress: return QStringLiteral("progress");
    case LogKind::Frame: return QStringLiteral("frame");
    case LogKind::Decision: return QStringLiteral("decision");
    case LogKind::Pause: return QStringLiteral("stall");
    case LogKind::Resume: return QStringLiteral("resume");
    case LogKind::Policy: return QStringLiteral("policy");
    case LogKind::GuardOverride: return QStringLiteral("guard");
    case LogKind::Other: return QStringLiteral("other");
    }
    return {};
}

LogIndex::LogIndex(int nodeId)
    : nodeId_(nodeId)
{
}

LogIndex::~LogIndex() = default;

std::shared_ptr<const LogIndex> LogIndex::open(const QString& simLogPath,
                                               const QString& policyLogPath, int nodeId)
{
    // The constructor is private, so make_shared cannot be used.
    std::shared_ptr<LogIndex> index(new LogIndex(nodeId));
    QString error;
    index->texts_[0].mapped = MappedFile::open(simLogPath, &error);
    if (!index->texts_[0].mapped)
        index->errors_.append(error);
    index->texts_[1].mapped = MappedFile::open(policyLogPath, &error);
    if (!index->texts_[1].mapped)
        index->errors_.append(error);
    index->build();
    return index;
}

std::shared_ptr<const LogIndex> LogIndex::fromText(QByteArray simLog, QByteArray policyLog,
                                                   int nodeId)
{
    std::shared_ptr<LogIndex> index(new LogIndex(nodeId));
    index->texts_[0].owned = std::move(simLog);
    index->texts_[1].owned = std::move(policyLog);
    index->build();
    return index;
}

void LogIndex::build()
{
    for (Text& text : texts_)
        text.lower = text.view().toByteArray().toLower();
    indexSim();
    indexPolicy();
    // Merge by time. The sort is stable, so lines with the same time keep their file order and
    // `sim.log` lines come before `policy.log` lines.
    std::ranges::stable_sort(lines_, {}, &LogLine::time);

    std::set<int> ues;
    for (const LogLine& line : lines_) {
        if (line.ue >= 0)
            ues.insert(line.ue);
    }
    ues_ = QList<int>(ues.begin(), ues.end());
}

void LogIndex::indexSim()
{
    const QByteArrayView text = texts_[0].view();
    double lastTime = 0.0;
    bool started = false;  // a timed line has been seen
    forEachLine(text, [&](QByteArrayView line, qsizetype number) {
        if (line.trimmed().isEmpty())
            return;
        LogLine entry;
        entry.source = LogSource::Sim;
        entry.offset = static_cast<qint32>(line.data() - text.data());
        entry.length = static_cast<qint32>(line.size());
        entry.lineNumber = static_cast<qint32>(number);

        std::optional<double> time;
        if (line.contains("PLAYING FRAME")) {
            const Tokens tokens(line);
            entry.kind = LogKind::Frame;
            time = toDouble(tokens.at(1));
            if (const auto player = tokens.intAfter("PlayerId:"))
                entry.ue = static_cast<qint16>(*player + (nodeId_ - playerIdForNode(nodeId_)));
        } else if (line.startsWith("Simulator Time:")) {
            entry.kind = LogKind::Progress;
            time = Tokens(line).doubleAfter("Time:");
        } else if (line.contains(" ue-id: ") && line.contains(" segmentId: ")) {
            const Tokens tokens(line);
            entry.kind = LogKind::Decision;
            time = toDouble(tokens.at(0));
            entry.ue = toUe(tokens.intAfter("ue-id:"));
        } else if (line.startsWith("[MPEG_PLAYER]")
                   && (line.contains("No frames to play") || line.contains("Play resumed"))) {
            const Tokens tokens(line);
            entry.kind = line.contains("Play resumed") ? LogKind::Resume : LogKind::Pause;
            time = assignment(tokens, "t=");
            if (const auto player = assignment(tokens, "ID="))
                entry.ue = static_cast<qint16>(static_cast<int>(*player)
                                               + (nodeId_ - playerIdForNode(nodeId_)));
        } else if (line.startsWith("[Python][LLM] node=")) {
            // The policy's decision as the simulation printed it; it has no time of its own.
            const Tokens tokens(line);
            entry.kind = line.contains("+guard(") ? LogKind::GuardOverride : LogKind::Policy;
            if (const auto node = toInt(tokens.at(1).sliced(5)))  // "node=7"
                entry.ue = static_cast<qint16>(*node);
        } else {
            entry.kind = started ? LogKind::Other : LogKind::Setup;
            if (line.startsWith("ue-id:"))
                entry.ue = toUe(Tokens(line).intAfter("ue-id:"));
        }

        if (time) {
            lastTime = *time;
            started = true;
        } else if (entry.kind != LogKind::Setup && entry.kind != LogKind::Other
                   && entry.kind != LogKind::Policy && entry.kind != LogKind::GuardOverride) {
            entry.kind = started ? LogKind::Other : LogKind::Setup;  // malformed timed line
        }
        entry.time = entry.kind == LogKind::Setup ? 0.0 : lastTime;
        lines_.push_back(entry);
    });
}

void LogIndex::indexPolicy()
{
    const QByteArrayView text = texts_[1].view();
    double lastTime = 0.0;
    forEachLine(text, [&](QByteArrayView line, qsizetype number) {
        if (line.trimmed().isEmpty())
            return;
        LogLine entry;
        entry.source = LogSource::Policy;
        entry.offset = static_cast<qint32>(line.data() - text.data());
        entry.length = static_cast<qint32>(line.size());
        entry.lineNumber = static_cast<qint32>(number);
        const Tokens tokens(line);
        if (const auto time = toDouble(tokens.at(0)); time && line.contains(" ue-id: ")) {
            lastTime = *time;
            entry.kind = line.contains("+guard(") ? LogKind::GuardOverride : LogKind::Policy;
            entry.ue = toUe(tokens.intAfter("ue-id:"));
        } else {
            entry.kind = line.startsWith('#') ? LogKind::Setup : LogKind::Other;
        }
        entry.time = entry.kind == LogKind::Setup ? 0.0 : lastTime;
        lines_.push_back(entry);
    });
}

QByteArrayView LogIndex::text(const LogLine& line) const
{
    return texts_[static_cast<std::size_t>(line.source)].view().sliced(line.offset, line.length);
}

QByteArrayView LogIndex::lowerText(const LogLine& line) const
{
    return QByteArrayView(texts_[static_cast<std::size_t>(line.source)].lower)
        .sliced(line.offset, line.length);
}

QString LogIndex::displayText(const LogLine& line) const
{
    return QString::fromUtf8(text(line));
}

qsizetype LogIndex::firstRowAt(double time) const
{
    const auto it = std::ranges::lower_bound(lines_, time, {}, &LogLine::time);
    return static_cast<qsizetype>(it - lines_.begin());
}

QFuture<LogIndexPtr> buildLogIndex(const QString& simLogPath, const QString& policyLogPath,
                                   int nodeId, QThreadPool* pool)
{
    if (!pool)
        pool = QThreadPool::globalInstance();
    return QtConcurrent::run(pool, [simLogPath, policyLogPath, nodeId](QPromise<LogIndexPtr>& promise) {
        if (promise.isCanceled())
            return;
        promise.addResult(LogIndex::open(simLogPath, policyLogPath, nodeId));
    });
}

} // namespace abrviz
