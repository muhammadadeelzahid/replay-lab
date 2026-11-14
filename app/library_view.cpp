#include "library_view.h"

#include "format.h"
#include "icons.h"

#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
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
        return bundle->cutOff ? tr("cut off") : tr("complete");
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

namespace {

QLabel* makeLabel(const QString& text, qreal scale, bool bold, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * scale);
    font.setBold(bold);
    label->setFont(font);
    return label;
}

} // namespace

LibraryView::LibraryView(QWidget* parent)
    : QWidget(parent)
    , model_(new LibraryModel(this))
    , proxy_(new QSortFilterProxyModel(this))
    , stack_(new QStackedWidget(this))
    , welcome_(new QWidget(this))
    , recentBox_(new QWidget(this))
    , browser_(new QWidget(this))
    , title_(makeLabel(QString(), 1.5, true, this))
    , path_(new QLabel(this))
    , search_(new QLineEdit(this))
    , table_(new QTableView(this))
    , details_(new QLabel(this))
    , openButton_(new QPushButton(tr("Open Run"), this))
{
    setObjectName(QStringLiteral("libraryView"));

    // Welcome screen.
    {
        auto* icon = new QLabel(welcome_);
        icon->setPixmap(appIcon().pixmap(112, 112));
        icon->setAlignment(Qt::AlignCenter);
        auto* name = makeLabel(QStringLiteral("abr-viz"), 2.4, true, welcome_);
        name->setAlignment(Qt::AlignCenter);
        auto* tagline = new QLabel(tr("Replays ns-3 adaptive streaming simulations: the video each "
                                      "simulated viewer saw, segment by segment, with the stalls, "
                                      "the policy's decisions, the network and the logs."), welcome_);
        tagline->setWordWrap(true);
        tagline->setAlignment(Qt::AlignCenter);
        tagline->setFixedWidth(520);
        tagline->setMinimumHeight(tagline->heightForWidth(520));  // centred, so the layout ignores heightForWidth
        tagline->setForegroundRole(QPalette::PlaceholderText);
        auto* openFolder = new QPushButton(glyphIcon(Glyph::OpenFolder, palette().color(QPalette::ButtonText)),
                                           tr("Open Folder of Bundles..."), welcome_);
        openFolder->setDefault(true);
        openFolder->setMinimumHeight(34);
        auto* openBundle = new QPushButton(glyphIcon(Glyph::OpenBundle, palette().color(QPalette::ButtonText)),
                                           tr("Open One Bundle..."), welcome_);
        openBundle->setMinimumHeight(34);
        connect(openFolder, &QPushButton::clicked, this, &LibraryView::openFolderRequested);
        connect(openBundle, &QPushButton::clicked, this, &LibraryView::openBundleRequested);
        auto* buttons = new QHBoxLayout();
        buttons->addStretch(1);
        buttons->addWidget(openFolder);
        buttons->addWidget(openBundle);
        buttons->addStretch(1);
        auto* recentLayout = new QVBoxLayout(recentBox_);
        recentLayout->setContentsMargins(0, 16, 0, 0);

        auto* column = new QVBoxLayout(welcome_);
        column->addStretch(2);
        column->addWidget(icon);
        column->addSpacing(8);
        column->addWidget(name);
        column->addWidget(tagline, 0, Qt::AlignHCenter);
        column->addSpacing(18);
        column->addLayout(buttons);
        column->addWidget(recentBox_, 0, Qt::AlignHCenter);
        column->addStretch(3);
    }

    // Run browser.
    {
        proxy_->setSourceModel(model_);
        proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
        proxy_->setFilterKeyColumn(-1);  // any column
        table_->setModel(proxy_);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 12);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(LibraryModel::Video, QHeaderView::Stretch);
        table_->horizontalHeader()->setHighlightSections(false);
        table_->setTextElideMode(Qt::ElideRight);
        table_->setAlternatingRowColors(true);
        table_->setShowGrid(false);
        table_->setFrameShape(QFrame::NoFrame);

        search_->setPlaceholderText(tr("Filter runs"));
        search_->setClearButtonEnabled(true);
        search_->setFixedWidth(260);
        connect(search_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
        path_->setForegroundRole(QPalette::PlaceholderText);
        path_->setTextInteractionFlags(Qt::TextSelectableByMouse);

        auto* header = new QHBoxLayout();
        auto* titles = new QVBoxLayout();
        titles->setSpacing(0);
        titles->addWidget(title_);
        titles->addWidget(path_);
        header->addLayout(titles, 1);
        header->addWidget(search_, 0, Qt::AlignBottom);

        auto* card = new QFrame(browser_);
        card->setObjectName(QStringLiteral("card"));
        card->setFixedWidth(300);
        details_->setWordWrap(true);
        details_->setTextFormat(Qt::RichText);
        details_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        openButton_->setDefault(true);
        openButton_->setMinimumHeight(32);
        openButton_->setEnabled(false);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->addWidget(details_, 1);
        cardLayout->addWidget(openButton_);

        auto* body = new QHBoxLayout();
        body->setSpacing(14);
        body->addWidget(table_, 1);
        body->addWidget(card);

        auto* layout = new QVBoxLayout(browser_);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(12);
        layout->addLayout(header);
        layout->addLayout(body, 1);

        connect(table_, &QTableView::activated, this, [this](const QModelIndex&) {
            if (const BundleSummaryPtr bundle = selectedBundle())
                emit openRequested(bundle->directory);
        });
        connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
                &LibraryView::selectionChanged);
        connect(openButton_, &QPushButton::clicked, this, [this] {
            if (const BundleSummaryPtr bundle = selectedBundle())
                emit openRequested(bundle->directory);
        });
    }

    stack_->addWidget(welcome_);
    stack_->addWidget(browser_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);
    showDetails(nullptr);
}

void LibraryView::setBundles(QList<BundleSummaryPtr> bundles, const QString& folder)
{
    title_->setText(QDir(folder).dirName());
    path_->setText(tr("%n run(s) in %1", nullptr, static_cast<int>(bundles.size()))
                       .arg(QDir::toNativeSeparators(folder)));
    model_->setBundles(std::move(bundles));
    stack_->setCurrentWidget(browser_);
    if (proxy_->rowCount() > 0 && !table_->currentIndex().isValid())
        table_->selectRow(0);
}

void LibraryView::setScanning(const QString& folder)
{
    title_->setText(QDir(folder).dirName());
    path_->setText(tr("Reading %1 ...").arg(QDir::toNativeSeparators(folder)));
    stack_->setCurrentWidget(browser_);
}

void LibraryView::setOpenBundle(const QString& directory)
{
    model_->setOpenBundle(directory);
    selectBundle(directory);
}

void LibraryView::selectBundle(const QString& directory)
{
    const int row = model_->rowOf(directory);
    if (row < 0)
        return;
    const QModelIndex index = proxy_->mapFromSource(model_->index(row, 0));
    if (index.isValid())
        table_->selectRow(index.row());
}

void LibraryView::setRecentFolders(const QStringList& folders)
{
    QLayout* layout = recentBox_->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    recentBox_->setVisible(!folders.isEmpty());
    if (folders.isEmpty())
        return;
    auto* heading = new QLabel(tr("Recent folders"), recentBox_);
    heading->setForegroundRole(QPalette::PlaceholderText);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    for (const QString& folder : folders) {
        auto* button = new QPushButton(glyphIcon(Glyph::OpenFolder, palette().color(QPalette::ButtonText)),
                                       QDir::toNativeSeparators(folder), recentBox_);
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, folder] { emit recentFolderRequested(folder); });
        layout->addWidget(button);
    }
}

int LibraryView::visibleRows() const
{
    return proxy_->rowCount();
}

bool LibraryView::isShowingWelcome() const
{
    return stack_->currentWidget() == welcome_;
}

QString LibraryView::detailsText() const
{
    return details_->text();
}

BundleSummaryPtr LibraryView::selectedBundle() const
{
    const QModelIndex index = table_->currentIndex();
    if (!index.isValid())
        return nullptr;
    return model_->bundleAt(proxy_->mapToSource(index).row());
}

void LibraryView::selectionChanged()
{
    showDetails(selectedBundle());
}

void LibraryView::showDetails(const BundleSummaryPtr& bundle)
{
    openButton_->setEnabled(bundle != nullptr && bundle->errors.isEmpty());
    if (!bundle) {
        details_->setText(tr("<p style='color:gray'>Select a run to see its details.</p>"));
        return;
    }
    auto row = [](const QString& name, const QString& value) {
        return QStringLiteral("<tr><td style='color:gray; padding-right:10px'>%1</td><td>%2</td></tr>")
            .arg(name.toHtmlEscaped(), value);
    };
    QString html = QStringLiteral("<h3 style='margin-bottom:2px'>%1</h3><p style='color:gray; margin-top:0'>%2</p>")
                       .arg(bundle->videoTitle.toHtmlEscaped(), bundle->runId.toHtmlEscaped());
    html += QStringLiteral("<table cellspacing='3'>");
    html += row(tr("Link"), bundle->linkRate ? format::bitrate(static_cast<double>(*bundle->linkRate))
                                             : tr("uncongested"));
    html += row(tr("Segments"), QString::number(bundle->segments));
    html += row(tr("Stalls"), bundle->stalls == 0
                                  ? tr("none")
                                  : QStringLiteral("<b style='color:#d0453a'>%1</b>").arg(bundle->stalls));
    html += row(tr("Playback"), bundle->cutOff ? tr("cut off by the end of the simulation")
                                               : tr("played to the end"));
    html += row(tr("Log match"), QStringLiteral("%1 %").arg(bundle->logDecisionAgreement * 100.0, 0, 'f', 0));
    html += QStringLiteral("</table>");
    for (const QString& error : bundle->errors)
        html += QStringLiteral("<p style='color:#d0453a'>%1</p>").arg(error.toHtmlEscaped());
    details_->setText(html);
}

} // namespace abrviz
