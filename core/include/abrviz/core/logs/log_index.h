#pragma once

#include "abrviz/core/io/mapped_file.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QFuture>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

class QThreadPool;

namespace abrviz {

enum class LogLevel : quint8 { Debug, Info, Warning };

enum class LogSource : quint8 { Sim, Policy };

/// What a log line is, as far as the viewer can tell.
enum class LogKind : quint8 {
    Setup,          ///< configuration echo before the simulation runs (no time)
    Progress,       ///< `Simulator Time: t`
    Frame,          ///< `PLAYING FRAME`
    Decision,       ///< `sim.log` ABR decision (`ue-id: ... segmentId: ...`)
    Pause,          ///< `[MPEG_PLAYER] No frames to play`: a stall starts
    Resume,         ///< `[MPEG_PLAYER] Play resumed`: a stall ends
    Policy,         ///< policy decision (`policy.log`, or `[Python][LLM]` in `sim.log`)
    GuardOverride,  ///< policy decision that the buffer guard lowered
    Other,          ///< any other line; it takes the time of the line before it
};

[[nodiscard]] LogLevel levelOf(LogKind kind);
[[nodiscard]] QString kindName(LogKind kind);

/// One line of `sim.log` or `policy.log`. The text stays in the index's buffers.
struct LogLine {
    double time = 0.0;      ///< simulation time; lines without one take the time before them
    qint32 offset = 0;      ///< byte offset of the text in its source
    qint32 length = 0;      ///< bytes, without the line ending
    qint32 lineNumber = 0;  ///< 1-based, in its file
    qint16 ue = -1;         ///< ns-3 node of the UE the line is about, or -1
    LogSource source = LogSource::Sim;
    LogKind kind = LogKind::Other;

    [[nodiscard]] LogLevel level() const { return levelOf(kind); }
};

/// Both logs of a bundle, merged into one list ordered by time. Built once on a worker thread
/// and then shared read-only as `std::shared_ptr<const LogIndex>`; filter tasks hold their own
/// reference, so the index outlives every task that reads it.
///
/// The text is not copied: the index owns the memory mappings of the two files and every line
/// refers into them. A lower-case copy of the text serves case-insensitive search.
class LogIndex {
public:
    /// Maps and indexes the two logs. A missing `policy.log` is not an error (`errors` lists
    /// it); a missing `sim.log` gives an empty index plus an error.
    [[nodiscard]] static std::shared_ptr<const LogIndex> open(const QString& simLogPath,
                                                              const QString& policyLogPath,
                                                              int nodeId);

    /// Indexes text held in memory (used by tests and tools).
    [[nodiscard]] static std::shared_ptr<const LogIndex> fromText(QByteArray simLog,
                                                                  QByteArray policyLog,
                                                                  int nodeId);

    LogIndex(const LogIndex&) = delete;
    LogIndex& operator=(const LogIndex&) = delete;
    LogIndex(LogIndex&&) = delete;
    LogIndex& operator=(LogIndex&&) = delete;
    ~LogIndex();

    [[nodiscard]] std::span<const LogLine> lines() const { return lines_; }
    [[nodiscard]] qsizetype size() const { return static_cast<qsizetype>(lines_.size()); }
    [[nodiscard]] const LogLine& line(qsizetype row) const
    {
        return lines_[static_cast<std::size_t>(row)];
    }

    /// The bytes of a line, as written and in lower case.
    [[nodiscard]] QByteArrayView text(const LogLine& line) const;
    [[nodiscard]] QByteArrayView lowerText(const LogLine& line) const;
    [[nodiscard]] QString displayText(const LogLine& line) const;

    /// ns-3 node of the bundle's UE.
    [[nodiscard]] int nodeId() const { return nodeId_; }
    /// Every UE that appears in the logs, ascending.
    [[nodiscard]] const QList<int>& ues() const { return ues_; }
    [[nodiscard]] const QStringList& errors() const { return errors_; }

    /// First row whose time is at least `time` (`size()` if none).
    [[nodiscard]] qsizetype firstRowAt(double time) const;

private:
    struct Text {
        std::optional<MappedFile> mapped;
        QByteArray owned;
        QByteArray lower;

        [[nodiscard]] QByteArrayView view() const
        {
            return mapped ? mapped->view() : QByteArrayView(owned);
        }
    };

    explicit LogIndex(int nodeId);
    void build();
    void indexSim();
    void indexPolicy();

    int nodeId_;
    std::array<Text, 2> texts_;  // by LogSource
    std::vector<LogLine> lines_;
    QList<int> ues_;
    QStringList errors_;
};

using LogIndexPtr = std::shared_ptr<const LogIndex>;

/// Builds the index of a bundle's logs on `pool`. The future yields one index (never null) and
/// can be cancelled before the work starts.
[[nodiscard]] QFuture<LogIndexPtr> buildLogIndex(const QString& simLogPath,
                                                 const QString& policyLogPath, int nodeId,
                                                 QThreadPool* pool = nullptr);

} // namespace abrviz
