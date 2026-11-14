#include "library_view.h"

#include "format.h"

#include <QDir>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>

namespace abrviz {

void LibraryModel::setBundles(QList<BundleSummaryPtr> bundles)
{
    beginResetModel();
    bundles_ = std::move(bundles);
    sortLibrary(bundles_);
    endResetModel();
}

void LibraryModel::setOpenBundle(const QString& directory)
{
    openBundle_ = QDir(directory).absolutePath();
    if (!bundles_.isEmpty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1), {Qt::FontRole});
}

BundleSummaryPtr LibraryModel::bundleAt(int row) const
{
    return row >= 0 && row < bundles_.size() ? bundles_[row] : BundleSummaryPtr();
}

int LibraryModel::rowOf(const QString& directory) const
{
    const QString absolute = QDir(directory).absolutePath();
    for (qsizetype i = 0; i < bundles_.size(); ++i) {
        if (bundles_[i]->directory == absolute)
            return static_cast<int>(i);
    }
    return -1;
}

int LibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(bundles_.size());
}

int LibraryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    const BundleSummaryPtr bundle = bundleAt(index.row());
    if (!bundle)
        return {};

    if (role == Qt::ToolTipRole) {
        return bundle->errors.isEmpty() ? bundle->directory
                                        : bundle->errors.join(QLatin1Char('\n'));
    }
    if (role == Qt::FontRole && bundle->directory == openBundle_) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::TextAlignmentRole && index.column() != Video)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case Video:
        return bundle->videoTitle.isEmpty() ? bundle->videoName : bundle->videoTitle;
    case LinkRate:
        return bundle->linkRate ? format::bitrate(static_cast<double>(*bundle->linkRate))
                                : tr("uncongested");
    case Segments:
        return bundle->segments;
    case Stalls:
        return bundle->stalls;
    case CutOff:
        return bundle->cutOff ? tr("cut off") : QString();
    case LogAgreement:
        return QStringLiteral("%1%").arg(bundle->logDecisionAgreement * 100.0, 0, 'f', 0);
    default:
        return {};
    }
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Video: return tr("Video");
    case LinkRate: return tr("Link");
    case Segments: return tr("Segments");
    case Stalls: return tr("Stalls");
    case CutOff: return tr("Playback");
    case LogAgreement: return tr("Log match");
    default: return {};
    }
}

LibraryView::LibraryView(QWidget* parent)
    : QWidget(parent)
    , model_(new LibraryModel(this))
    , table_(new QTableView(this))
    , header_(new QLabel(this))
{
    setObjectName(QStringLiteral("libraryView"));
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);

    header_->setText(tr("Open a folder of bundles (File, Open Folder)."));
    header_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(header_);
    layout->addWidget(table_);

    auto open = [this](const QModelIndex& index) {
        if (const BundleSummaryPtr bundle = model_->bundleAt(index.row()))
            emit openRequested(bundle->directory);
    };
    connect(table_, &QTableView::activated, this, open);
}

void LibraryView::setBundles(QList<BundleSummaryPtr> bundles, const QString& folder)
{
    header_->setText(tr("%n bundle(s) in %1", nullptr, static_cast<int>(bundles.size()))
                         .arg(QDir(folder).dirName()));
    model_->setBundles(std::move(bundles));
}

void LibraryView::setScanning(const QString& folder)
{
    header_->setText(tr("Reading %1 ...").arg(QDir(folder).dirName()));
}

void LibraryView::setOpenBundle(const QString& directory)
{
    model_->setOpenBundle(directory);
    const int row = model_->rowOf(directory);
    if (row >= 0)
        table_->selectRow(row);
}

} // namespace abrviz
