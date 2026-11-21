#pragma once

/**
 * @file
 * @brief The QoE of every ABR algorithm of an experiment, side by side.
 */

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/load_result.h"
#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/util/future_guard.h"

#include <QFutureWatcher>
#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QTableWidget;

namespace abrviz {

/**
 * @brief A table with one row per algorithm of the open run's experiment: average bitrate,
 *        mean and lowest-segment VMAF, time below VMAF 60, switches, stalls, stalled time and
 *        startup.
 *
 * The best value of each column is marked. The runs are loaded in the background; the open
 * run's row is in bold. Clicking another algorithm's row asks for it side by side with the
 * open run.
 */
class Scoreboard final : public QWidget {
    Q_OBJECT

public:
    /** @brief The table's columns. */
    enum Column { Algorithm, Bitrate, VmafMean, VmafMin, BelowVmaf60, Switches, Stalls, StalledTime, Startup, ColumnCount };

    /**
     * @brief Creates an empty scoreboard.
     *
     * @param parent The parent widget.
     */
    explicit Scoreboard(QWidget* parent = nullptr);

    /// Cancels the loads still running.
    ~Scoreboard() override;

    /**
     * @brief Shows an experiment.
     *
     * @param runs The experiment's runs, LLM first; fewer than two hides the scoreboard.
     * @param open The directory of the open run.
     */
    void setExperiment(const QList<BundleSummaryPtr>& runs, const QString& open);

    /// @return The number of rows filled so far.
    [[nodiscard]] int rowsFilled() const;

    /**
     * @brief The text of a cell, for tests.
     *
     * @param row The row.
     * @param column The column.
     * @return The text; empty if out of range.
     */
    [[nodiscard]] QString cell(int row, Column column) const;

    /**
     * @brief Tells whether a cell is marked as the best of its column, for tests.
     *
     * @param row The row.
     * @param column The column.
     * @return True if marked.
     */
    [[nodiscard]] bool isBest(int row, Column column) const;

signals:
    /**
     * @brief Another algorithm's row was clicked.
     *
     * @param directory The run's bundle directory.
     */
    void compareRequested(const QString& directory);

    /// Every run's row is filled.
    void filled();

private:
    /// One run being loaded or shown.
    struct Entry {
        BundleSummaryPtr bundle;               ///< the run
        std::optional<QoeReport> report;       ///< its QoE, once loaded
        FutureGuard<LoadResult> load;          ///< the pending load
        QFutureWatcher<LoadResult>* watcher = nullptr;  ///< reports the load
    };

    /// Writes the table from the entries, marking the best values.
    void fillTable();

    QLabel* title_;            ///< "Compared with" heading.
    QTableWidget* table_;      ///< The rows.
    QString open_;             ///< The open run's directory.
    std::vector<std::unique_ptr<Entry>> entries_;  ///< In row order.
};

} // namespace abrviz
