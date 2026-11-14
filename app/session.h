#pragma once

/**
 * @file
 * @brief The open run, its derived data and the shared time cursor.
 */

#include "abrviz/core/logs/log_index.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/model/run_data.h"
#include "abrviz/core/playback/playback_clock.h"

#include <QObject>
#include <QStringList>

#include <memory>

namespace abrviz {

class TimeCursor;

/**
 * @brief The open run and everything derived from it.
 *
 * Panels read from the session when `runChanged` is emitted and keep their own `shared_ptr`
 * to the run. The log index is built after the run is loaded and arrives separately
 * (`logIndexChanged`). The session owns the time cursor.
 */
class Session final : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Creates an empty session and its time cursor.
     *
     * @param parent Owning QObject, or null.
     */
    explicit Session(QObject* parent = nullptr);
    /**
     * @brief Destroys the session; the time cursor is deleted with it as a child object.
     */
    ~Session() override;

    /**
     * @brief Replaces the open run.
     *
     * Drops the log index, rebuilds the playback clock and the QoE report, sets the cursor range
     * to [0, endTime()] and moves the cursor to the start of playback (Source::Program). Emits
     * `runChanged`, then `logIndexChanged` if a log index was dropped.
     *
     * @param run The new run, or null to close the current one.
     * @param loadErrors Problems found while loading.
     */
    void setRun(std::shared_ptr<const RunData> run, QStringList loadErrors = {});
    /**
     * @brief Closes the open run; same as `setRun(nullptr)`.
     */
    void clear();
    /**
     * @brief Sets the index of the open run's logs and emits `logIndexChanged`.
     *
     * Ignored if `index` is for another run (or no run is open), so an index that finishes
     * building after the run was replaced is dropped.
     *
     * @param index The log index.
     * @param forRun The run the index was built for.
     */
    void setLogIndex(LogIndexPtr index, const RunData* forRun);

    /**
     * @brief The open run.
     *
     * @return The run, or null if none is open.
     */
    [[nodiscard]] const std::shared_ptr<const RunData>& run() const { return run_; }
    /**
     * @brief Playback clock of the open run.
     *
     * @return The clock; a default clock if no run is open.
     */
    [[nodiscard]] const PlaybackClock& clock() const { return clock_; }
    /**
     * @brief QoE metrics of the open run.
     *
     * @return The report; an empty report if no run is open.
     */
    [[nodiscard]] const QoeReport& report() const { return report_; }
    /**
     * @brief Problems found while loading the open run.
     *
     * @return The error messages, possibly empty.
     */
    [[nodiscard]] const QStringList& loadErrors() const { return loadErrors_; }
    /**
     * @brief The time cursor shared by all panels.
     *
     * @return The cursor, owned by the session; never null.
     */
    [[nodiscard]] TimeCursor* cursor() const { return cursor_; }
    /**
     * @brief Index of the open run's logs.
     *
     * @return The index, or null while it is being built or if no run is open.
     */
    [[nodiscard]] const LogIndexPtr& logIndex() const { return logIndex_; }

    /**
     * @brief End of the time axis shown by the panels.
     *
     * @return The latest of the playback end, the simulation end and the download end of the
     *         last segment, in seconds; 0 if no run is open.
     */
    [[nodiscard]] double endTime() const;

signals:
    /**
     * @brief Emitted after the open run was replaced or closed.
     */
    void runChanged();
    /**
     * @brief Emitted when the log index arrives or is dropped with the run it belonged to.
     */
    void logIndexChanged();

private:
    std::shared_ptr<const RunData> run_;  ///< The open run; null if none.
    PlaybackClock clock_;  ///< Playback clock of run_; a default clock if no run is open.
    QoeReport report_;    ///< QoE metrics of run_; empty if no run is open.
    QStringList loadErrors_;  ///< Problems found while loading run_.
    LogIndexPtr logIndex_;  ///< Index of run_'s logs; null until set or if no run is open.
    TimeCursor* cursor_;  ///< Child object.
};

} // namespace abrviz
