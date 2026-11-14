#pragma once

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

/// The filtered lines of a `LogIndex`, as a table.
class LogModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Time, Ue, Source, Text, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    /// Replaces the index and empties the rows.
    void setIndex(LogIndexPtr index);
    void setRows(LogRows rows);

    [[nodiscard]] const LogIndexPtr& logIndex() const { return index_; }
    [[nodiscard]] const LogRows& rows() const { return rows_; }
    [[nodiscard]] const LogLine* lineAt(int row) const;
    /// The last row at or before `time` (the line "current" at that time), or -1.
    [[nodiscard]] int rowAtTime(double time) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    LogIndexPtr index_;
    LogRows rows_;
};

/// Both logs of the open run, with text, UE and level filters. Filtering runs on the thread
/// pool: typing restarts a 150 ms timer, and each request replaces the previous one without
/// waiting for it. Results of older requests are discarded. The table follows the cursor, and
/// selecting a line moves the cursor to its time.
class LogConsole final : public QWidget {
    Q_OBJECT

public:
    explicit LogConsole(Session* session, QWidget* parent = nullptr);
    ~LogConsole() override;

    LogConsole(const LogConsole&) = delete;
    LogConsole& operator=(const LogConsole&) = delete;
    LogConsole(LogConsole&&) = delete;
    LogConsole& operator=(LogConsole&&) = delete;

    [[nodiscard]] LogFilterSpec spec() const;
    [[nodiscard]] LogModel* model() const { return model_; }
    [[nodiscard]] QLineEdit* filterEdit() const { return text_; }
    [[nodiscard]] QComboBox* ueBox() const { return ue_; }
    [[nodiscard]] QComboBox* levelBox() const { return level_; }
    [[nodiscard]] QTableView* table() const { return table_; }
    /// True while a request is pending (including the debounce delay).
    [[nodiscard]] bool isFiltering() const;
    /// Results that arrived after a newer request and were dropped.
    [[nodiscard]] int discardedResults() const { return discarded_; }
    void setFollowCursor(bool follow);

signals:
    /// The table now shows the result of request `generation`.
    void filterApplied(quint64 generation);

private:
    void indexChanged();
    void requestFilter();
    void filterFinished();
    void followCursor(double time);
    void rowSelected(const QModelIndex& current);
    void updateStatus();

    Session* session_;  // not owned; outlives the console
    LogIndexPtr index_;
    LogFilter filter_;
    FutureGuard<LogFilterResult> current_;
    std::vector<FutureGuard<LogFilterResult>> retiring_;  // cancelled, maybe still running
    QFutureWatcher<LogFilterResult>* watcher_;
    QTimer* debounce_;
    LogModel* model_;
    QLineEdit* text_;
    QComboBox* ue_;
    QComboBox* level_;
    QCheckBox* follow_;
    QLabel* status_;
    QTableView* table_;
    bool selecting_ = false;  // the console is selecting a row itself
    int discarded_ = 0;
};

} // namespace abrviz
