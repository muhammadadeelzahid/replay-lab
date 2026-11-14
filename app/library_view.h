#pragma once

#include "abrviz/core/load/bundle_library.h"

#include <QAbstractTableModel>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QTableView;

namespace abrviz {

/// Rows of the library: one per bundle, sorted by video and link rate.
class LibraryModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Video, LinkRate, Segments, Stalls, CutOff, LogAgreement, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    void setBundles(QList<BundleSummaryPtr> bundles);
    void setOpenBundle(const QString& directory);
    [[nodiscard]] BundleSummaryPtr bundleAt(int row) const;
    [[nodiscard]] int rowOf(const QString& directory) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<BundleSummaryPtr> bundles_;
    QString openBundle_;
};

/// The Library page. Without a folder it shows a welcome screen (open a folder or a bundle,
/// recent folders); with one, a searchable table of its runs and the details of the selected
/// run. Double-click, Enter or the Open button opens a run.
class LibraryView final : public QWidget {
    Q_OBJECT

public:
    explicit LibraryView(QWidget* parent = nullptr);

    void setBundles(QList<BundleSummaryPtr> bundles, const QString& folder);
    void setScanning(const QString& folder);
    void setOpenBundle(const QString& directory);
    void setRecentFolders(const QStringList& folders);

    [[nodiscard]] LibraryModel* model() const { return model_; }
    [[nodiscard]] QLineEdit* searchEdit() const { return search_; }
    /// Rows shown after the search filter.
    [[nodiscard]] int visibleRows() const;
    [[nodiscard]] bool isShowingWelcome() const;
    [[nodiscard]] QString detailsText() const;
    /// Selects the row of `directory` (if listed and not filtered out).
    void selectBundle(const QString& directory);

signals:
    void openRequested(const QString& directory);
    void openFolderRequested();
    void openBundleRequested();
    void recentFolderRequested(const QString& folder);

private:
    void selectionChanged();
    void showDetails(const BundleSummaryPtr& bundle);
    [[nodiscard]] BundleSummaryPtr selectedBundle() const;

    LibraryModel* model_;
    QSortFilterProxyModel* proxy_;
    QStackedWidget* stack_;
    QWidget* welcome_;
    QWidget* recentBox_;
    QWidget* browser_;
    QLabel* title_;
    QLabel* path_;
    QLineEdit* search_;
    QTableView* table_;
    QLabel* details_;
    QPushButton* openButton_;
};

} // namespace abrviz
