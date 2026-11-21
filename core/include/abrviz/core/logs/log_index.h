#pragma once

/**
 * @file
 * @brief A time-ordered index of the lines of `sim.log` and `policy.log`.
 */

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

/**
 * @brief Importance of a log line, used to hide detail in the console.
 */
enum class LogLevel : quint8 { Debug, Info, Warning };

/**
 * @brief The file a log line comes from.
 */
enum class LogSource : quint8 { Sim, Policy };

/**
 * @brief What a log line is, as far as the viewer can tell.
 */
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

/**
 * @brief Returns the level of a kind of line.
 *
 * @param kind The kind of line.
 * @return `Debug` for progress and frame lines, `Warning` for pauses and guard overrides,
 *         `Info` for the rest.
 */
[[nodiscard]] LogLevel levelOf(LogKind kind);

/**
 * @brief Returns the short name of a kind of line, as shown in the console.
 *
 * @param kind The kind of line.
 * @return A lower-case name, for example "stall" for `Pause` or "guard" for `GuardOverride`.
 */
[[nodiscard]] QString kindName(LogKind kind);

/**
 * @brief One line of `sim.log` or `policy.log`.
 *
 * The text stays in the index's buffers.
 */
struct LogLine {
    double time = 0.0;      ///< simulation time; lines without one take the time before them
    qint32 offset = 0;      ///< byte offset of the text in its source
    qint32 length = 0;      ///< bytes, without the line ending
    qint32 lineNumber = 0;  ///< 1-based, in its file
    qint16 ue = -1;         ///< ns-3 node of the UE the line is about, or -1
    LogSource source = LogSource::Sim;  ///< file the line comes from
    LogKind kind = LogKind::Other;      ///< what the line is

    /**
     * @brief Returns the level of the line.
     *
     * @return `levelOf(kind)`.
     */
    [[nodiscard]] LogLevel level() const { return levelOf(kind); }
};

/**
 * @brief Both logs of a bundle, merged into one list ordered by time.
 *
 * Built once on a worker thread and then shared read-only as `std::shared_ptr<const LogIndex>`;
 * filter tasks hold their own reference, so the index outlives every task that reads it.
 *
 * The text is not copied: the index owns the memory mappings of the two files and every line
 * refers into them. A lower-case copy of the text serves case-insensitive search.
 */
class LogIndex {
public:
    /**
     * @brief Maps and indexes the two logs.
     *
     * A missing `policy.log` is not an error (`errors` lists it); a missing `sim.log` gives an
     * empty index plus an error.
     *
     * @param simLogPath Path of `sim.log`.
     * @param policyLogPath Path of `policy.log`; empty if the run has none.
     * @param nodeId ns-3 node of the bundle's UE.
     * @return The index; never null.
     */
    [[nodiscard]] static std::shared_ptr<const LogIndex> open(const QString& simLogPath,
                                                              const QString& policyLogPath,
                                                              int nodeId);

    /**
     * @brief Indexes text held in memory (used by tests and tools).
     *
     * @param simLog Contents of `sim.log`; owned by the index.
     * @param policyLog Contents of `policy.log`; owned by the index.
     * @param nodeId ns-3 node of the bundle's UE.
     * @return The index; never null.
     */
    [[nodiscard]] static std::shared_ptr<const LogIndex> fromText(QByteArray simLog,
                                                                  QByteArray policyLog,
                                                                  int nodeId);

    /** @brief Neither copyable nor movable: lines refer into the buffers the index owns. */
    LogIndex(const LogIndex&) = delete;
    LogIndex& operator=(const LogIndex&) = delete;
    LogIndex(LogIndex&&) = delete;
    LogIndex& operator=(LogIndex&&) = delete;

    /**
     * @brief Releases the mappings and buffers.
     */
    ~LogIndex();

    /**
     * @brief Returns every line in time order.
     *
     * @return The lines.
     */
    [[nodiscard]] std::span<const LogLine> lines() const { return lines_; }

    /**
     * @brief Returns the number of lines.
     *
     * @return The line count.
     */
    [[nodiscard]] qsizetype size() const { return static_cast<qsizetype>(lines_.size()); }

    /**
     * @brief Returns one line.
     *
     * @param row Row in the index, in `[0, size())`; not checked.
     * @return The line.
     */
    [[nodiscard]] const LogLine& line(qsizetype row) const
    {
        return lines_[static_cast<std::size_t>(row)];
    }

    /**
     * @brief Returns the bytes of a line as written.
     *
     * @param line A line of this index.
     * @return A view into the index's buffer, without the line ending.
     */
    [[nodiscard]] QByteArrayView text(const LogLine& line) const;

    /**
     * @brief Returns the bytes of a line in lower case.
     *
     * @param line A line of this index.
     * @return A view into the index's lower-case copy.
     */
    [[nodiscard]] QByteArrayView lowerText(const LogLine& line) const;

    /**
     * @brief Returns the text of a line decoded from UTF-8.
     *
     * @param line A line of this index.
     * @return The text.
     */
    [[nodiscard]] QString displayText(const LogLine& line) const;

    /**
     * @brief Returns the ns-3 node of the bundle's UE.
     *
     * @return The node id.
     */
    [[nodiscard]] int nodeId() const { return nodeId_; }

    /**
     * @brief Returns every UE that appears in the logs.
     *
     * @return The node ids, ascending.
     */
    [[nodiscard]] const QList<int>& ues() const { return ues_; }

    /**
     * @brief Returns the problems found while opening the logs.
     *
     * @return One message per file that could not be read.
     */
    [[nodiscard]] const QStringList& errors() const { return errors_; }

    /**
     * @brief Returns the first row whose time is at least `time`.
     *
     * @param time Simulation time in seconds.
     * @return The row, or `size()` if none.
     */
    [[nodiscard]] qsizetype firstRowAt(double time) const;

private:
    /**
     * @brief The text of one log: either mapped or owned, plus a lower-case copy.
     */
    struct Text {
        /** @brief Memory mapping of the file; empty when the text is in `owned` or unmapped. */
        std::optional<MappedFile> mapped;
        QByteArray owned;  ///< used when `mapped` is empty
        QByteArray lower;  ///< lower-case copy of the text, for case-insensitive search

        /**
         * @brief Returns the text of the log.
         *
         * @return The mapped bytes if there is a mapping, otherwise `owned`.
         */
        [[nodiscard]] QByteArrayView view() const
        {
            return mapped ? mapped->view() : QByteArrayView(owned);
        }
    };

    /**
     * @brief Creates an empty index; use open() or fromText().
     *
     * @param nodeId ns-3 node of the bundle's UE.
     */
    explicit LogIndex(int nodeId);

    /**
     * @brief Builds the lower-case copies, indexes both logs and merges them by time.
     *
     * The sort is stable, so lines with the same time keep their file order and `sim.log`
     * lines come before `policy.log` lines. Also collects the UEs.
     */
    void build();

    /**
     * @brief Adds the lines of `sim.log`, classifying each one.
     *
     * Lines before the first timed line are setup lines at time 0; later lines without a time
     * take the time of the timed line before them. Empty lines are skipped.
     */
    void indexSim();

    /**
     * @brief Adds the lines of `policy.log`.
     *
     * Decision lines are `Policy` or `GuardOverride`; lines starting with `#` are setup lines at
     * time 0; other lines take the time of the decision before them. Empty lines are skipped.
     */
    void indexPolicy();

    int nodeId_;                  ///< ns-3 node of the bundle's UE
    std::array<Text, 2> texts_;   ///< `sim.log` and `policy.log`, indexed by `LogSource`
    std::vector<LogLine> lines_;  ///< lines of both logs, in time order
    QList<int> ues_;              ///< node ids of every UE in the logs, ascending
    QStringList errors_;          ///< one message per log that could not be mapped
};

/** @brief Shared, immutable log index. */
using LogIndexPtr = std::shared_ptr<const LogIndex>;

/**
 * @brief Builds the index of a bundle's logs on `pool`.
 *
 * The future yields one index (never null) and can be cancelled before the work starts; a
 * cancelled build yields no result.
 *
 * @param simLogPath Path of `sim.log`.
 * @param policyLogPath Path of `policy.log`; empty if the run has none.
 * @param nodeId ns-3 node of the bundle's UE.
 * @param pool Thread pool to run on; null for the global pool.
 * @return The future of the index.
 * @see LogIndex::open()
 */
[[nodiscard]] QFuture<LogIndexPtr> buildLogIndex(const QString& simLogPath,
                                                 const QString& policyLogPath, int nodeId,
                                                 QThreadPool* pool = nullptr);

} // namespace abrviz
