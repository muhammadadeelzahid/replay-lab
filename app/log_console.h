#pragma once

/**
 * @file
 * @brief The log console: a filtered table of both logs of the open run.
 */

#include "abrviz/core/logs/log_filter.h"
#include "abrviz/core/util/future_guard.h"

#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class QTimer;

namespace abrviz {

class Session;

/**
 * @brief The filtered lines of a `LogIndex`, as a table.
 *
 * Each row is a position in the index (from `LogRows`); the lines themselves stay in the index.
 */
class LogModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    /**
     * @brief The table columns; ColumnCount is the number of columns.
     */
    enum Column { Time, Ue, Source, Text, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    /**
     * @brief Replaces the index and empties the rows (resets the model).
     *
     * @param index The new index, or null.
     */
    void setIndex(LogIndexPtr index);
    /**
     * @brief Replaces the rows shown (resets the model).
     *
     * @param rows Positions of the lines to show in the current index, in time order.
     */
    void setRows(LogRows rows);

    /**
     * @brief The index the rows refer to.
     *
     * @return The index, or null if none is set.
     */
    [[nodiscard]] const LogIndexPtr& logIndex() const { return index_; }
    /**
     * @brief The rows shown.
     *
     * @return Positions of the lines in the index.
     */
    [[nodiscard]] const LogRows& rows() const { return rows_; }
    /**
     * @brief The log line of a row.
     *
     * @param row Table row.
     * @return The line, owned by the index, or null if `row` is out of range or there is no
     *         index.
     */
    [[nodiscard]] const LogLine* lineAt(int row) const;
    /**
     * @brief The last row at or before a time (the line "current" at that time).
     *
     * @param time Simulation time in seconds.
     * @return The row, or -1 if there is none (no index, no rows, or every row is later).
     */
    [[nodiscard]] int rowAtTime(double time) const;

    /**
     * @brief Number of rows shown.
     *
     * @param parent Parent index; a valid parent has no children.
     * @return The row count, or 0 for a valid parent.
     */
    int rowCount(const QModelIndex& parent = {}) const override;
    /**
     * @brief Number of columns.
     *
     * @param parent Parent index; a valid parent has no children.
     * @return ColumnCount, or 0 for a valid parent.
     */
    int columnCount(const QModelIndex& parent = {}) const override;
    /**
     * @brief Data of a cell.
     *
     * Provides the display text (time with three decimals, empty for setup lines; UE; source
     * and line number; text), tooltips (the full text, or the line kind), colours for warning
     * and debug lines, and right alignment for the time and UE columns.
     *
     * @param index The cell.
     * @param role The data role.
     * @return The value, or an invalid QVariant if there is none for this cell and role.
     */
    QVariant data(const QModelIndex& index, int role) const override;
    /**
     * @brief Column titles.
     *
     * @param section Column number.
     * @param orientation Only horizontal headers have titles.
     * @param role Only Qt::DisplayRole has a value.
     * @return The title, or an invalid QVariant.
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    LogIndexPtr index_;  ///< Index the rows refer to; null if none.
    LogRows rows_;       ///< Line numbers in index_ of the rows, in table order.
};

/**
 * @brief Both logs of the open run, with text, UE and level filters.
 *
 * Filtering runs on the thread pool: typing restarts a 150 ms timer, and each request replaces
 * the previous one without waiting for it. Results of older requests are discarded. The table
 * follows the cursor, and selecting a line moves the cursor to its time (Source::Logs); the
 * console ignores cursor moves it caused itself.
 */
class LogConsole final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the console and loads the session's log index, if any.
     *
     * @param session Session to read from; not owned, must outlive the console.
     * @param parent Parent widget, or null.
     */
    explicit LogConsole(Session* session, QWidget* parent = nullptr);
    /**
     * @brief Cancels the running filter request and waits for the filter tasks to finish.
     *
     * No result can arrive while the members are destroyed.
     */
    ~LogConsole() override;

    /** @brief Not copyable or movable. */
    LogConsole(const LogConsole&) = delete;
    LogConsole& operator=(const LogConsole&) = delete;
    LogConsole(LogConsole&&) = delete;
    LogConsole& operator=(LogConsole&&) = delete;

    /**
     * @brief The filter set in the controls.
     *
     * @return The filter text, the chosen UE (or all UEs) and the lowest level shown.
     */
    [[nodiscard]] LogFilterSpec spec() const;
    /**
     * @brief The table model.
     *
     * @return The model, owned by the console.
     */
    [[nodiscard]] LogModel* model() const { return model_; }
    /**
     * @brief The filter text field.
     *
     * @return The field, owned by the console.
     */
    [[nodiscard]] QLineEdit* filterEdit() const { return text_; }
    /**
     * @brief The UE choice.
     *
     * @return The combo box, owned by the console.
     */
    [[nodiscard]] QComboBox* ueBox() const { return ue_; }
    /**
     * @brief The level choice.
     *
     * @return The combo box, owned by the console.
     */
    [[nodiscard]] QComboBox* levelBox() const { return level_; }
    /**
     * @brief The table of log lines.
     *
     * @return The table view, owned by the console.
     */
    [[nodiscard]] QTableView* table() const { return table_; }
    /**
     * @brief Whether a filter change has not reached the table yet.
     *
     * @return True from a change of the filter until its result is in the table (including
     *         the debounce delay and the hand-over of the result to the GUI thread).
     */
    [[nodiscard]] bool isFiltering() const;
    /**
     * @brief Results that arrived after a newer request and were dropped.
     *
     * @return The number of dropped results since the console was created.
     */
    [[nodiscard]] int discardedResults() const { return discarded_; }
    /**
     * @brief Turns following the cursor on or off (the "Follow cursor" box).
     *
     * Turning it on selects the line at the cursor right away.
     *
     * @param follow True to follow the cursor.
     */
    void setFollowCursor(bool follow);

signals:
    /**
     * @brief The table now shows the result of a filter request.
     *
     * @param generation Number of the request whose result is shown.
     */
    void filterApplied(quint64 generation);

private:
    /**
     * @brief Takes the session's log index when it changed.
     *
     * Cancels the running request, empties the table, refills the UE choices (all UEs, or one
     * UE; the bundle's own UE is selected) and requests a filter for the new index.
     */
    void indexChanged();
    /**
     * @brief Starts a filter request with the current controls on the thread pool.
     *
     * Stops the debounce timer. The previous request is cancelled and kept in `retiring_`
     * until it finishes; finished ones are dropped. Does nothing without an index.
     */
    void requestFilter();
    /**
     * @brief Applies the result of the newest request when it finishes.
     *
     * A result from an older request is counted in discardedResults() and dropped. After
     * applying, emits `filterApplied` and selects the line at the cursor if following.
     */
    void filterFinished();
    /**
     * @brief Selects and centres the last line at or before a time.
     *
     * The selection does not move the cursor back (`selecting_` is set meanwhile).
     *
     * @param time Simulation time in seconds.
     */
    void followCursor(double time);
    /**
     * @brief Moves the cursor to the time of a line the user selected (Source::Logs).
     *
     * Ignored while the console selects a row itself, and for setup lines, which have no time.
     *
     * @param current The newly selected cell.
     */
    void rowSelected(const QModelIndex& current);
    /**
     * @brief Updates the status text: no run, reading logs, or the number of lines shown out
     *        of the total (and whether filtering is under way).
     */
    void updateStatus();

    Session* session_;  ///< Not owned; outlives the console.
    LogIndexPtr index_;  ///< Index of the run's logs; null while it is built or if no run is open.
    LogFilter filter_;  ///< Runs the filter requests and tells which result is current.
    FutureGuard<LogFilterResult> current_;  ///< The newest request.
    std::vector<FutureGuard<LogFilterResult>> retiring_;  ///< Cancelled, maybe still running.
    QFutureWatcher<LogFilterResult>* watcher_;  ///< Watches the newest request only.
    QTimer* debounce_;  ///< Delays a request while the user types.
    LogModel* model_;   ///< Table model of the filtered lines; child of the console.
    QLineEdit* text_;   ///< Filter text field.
    QComboBox* ue_;     ///< UE choice; "All UEs" or one UE id.
    QComboBox* level_;  ///< Lowest log level shown.
    QCheckBox* follow_;  ///< Whether the table follows the time cursor.
    QLabel* status_;    ///< Number of lines shown, or why there are none.
    QTableView* table_;  ///< The filtered lines.
    bool pending_ = false;    ///< A request was made and its result is not applied yet.
    bool selecting_ = false;  ///< The console is selecting a row itself.
    int discarded_ = 0;       ///< Results dropped because a newer request was made.
};

} // namespace abrviz
