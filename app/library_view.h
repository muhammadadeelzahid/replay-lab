#pragma once

#include "abrviz/core/load/bundle_library.h"

#include <QAbstractTableModel>
#include <QWidget>

class QLabel;
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

/// The table of bundles in the open folder. Double-click (or Enter) opens a bundle.
class LibraryView final : public QWidget {
    Q_OBJECT

public:
    explicit LibraryView(QWidget* parent = nullptr);

    void setBundles(QList<BundleSummaryPtr> bundles, const QString& folder);
    void setScanning(const QString& folder);
    void setOpenBundle(const QString& directory);
    [[nodiscard]] LibraryModel* model() const { return model_; }

signals:
    void openRequested(const QString& directory);

private:
    LibraryModel* model_;
    QTableView* table_;
    QLabel* header_;
};

} // namespace abrviz
