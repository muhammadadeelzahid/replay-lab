#pragma once

#include "abrviz/core/load/bundle_library.h"

#include <QTreeWidget>

namespace abrviz {

/// The window's sidebar, in the style of a macOS source list: a Library section with All Runs,
/// then one section per video listing its runs by link rate. Selecting All Runs shows the
/// library; selecting a run opens it.
class SourceList final : public QTreeWidget {
    Q_OBJECT

public:
    explicit SourceList(QWidget* parent = nullptr);

    /// Replaces the runs (in library order).
    void setRuns(const QList<BundleSummaryPtr>& runs);
    /// Selects the run in `directory` without emitting a signal (empty selects All Runs).
    void selectRun(const QString& directory);
    void selectAllRuns();
    /// Number of run items.
    [[nodiscard]] int runCount() const;
    /// The text of the run item for `directory` and its section, as "Section / Run".
    [[nodiscard]] QString runLabel(const QString& directory) const;

signals:
    void allRunsSelected();
    void runSelected(const QString& directory);

protected:
    void changeEvent(QEvent* event) override;

private:
    enum Role { KindRole = Qt::UserRole + 1, DirectoryRole };
    enum Kind { Section, AllRuns, Run };

    QTreeWidgetItem* addSection(const QString& title);
    void itemChanged(QTreeWidgetItem* current);
    void applyStyle();
    bool restyling_ = false;
    [[nodiscard]] QTreeWidgetItem* findRun(const QString& directory) const;

    QTreeWidgetItem* allRuns_ = nullptr;
    bool selecting_ = false;
};

} // namespace abrviz
