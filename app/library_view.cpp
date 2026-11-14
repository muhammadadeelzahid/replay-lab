#include "library_view.h"

#include "branding.h"
#include "format.h"
#include "home_widgets.h"
#include "icons.h"
#include "thumbnail_cache.h"
#include "welcome_scene.h"

#include "abrviz/core/io/topology_reader.h"
#include "abrviz/core/load/bundle_reader.h"

#include "abrviz/core/version.h"

#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <functional>

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

/**
 * @brief A label in a scaled, optionally bold, font.
 *
 * @param text The label text.
 * @param scale The font size relative to the label's default font.
 * @param bold Whether the font is bold.
 * @param parent The label's parent.
 * @return The new label, owned by `parent`.
 */
QLabel* makeLabel(const QString& text, qreal scale, bool bold, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font = format::scaled(font, scale);
    font.setBold(bold);
    label->setFont(font);
    return label;
}

/**
 * @brief A row of the welcome screen: an icon, a title and a line of explanation.
 *
 * The whole row is the button, as in Xcode's welcome window.
 *
 * @param glyph The icon, drawn in the accent colour of `parent`'s palette.
 * @param title The bold title.
 * @param detail The line of explanation, in secondary plain text.
 * @param action Called when the row is clicked.
 * @param parent The button's parent; must not be null.
 * @return The new flat button, owned by `parent`.
 */
QPushButton* actionRow(Glyph glyph, const QString& title, const QString& detail,
                       std::function<void()> action, QWidget* parent)
{
    auto* row = new QPushButton(parent);
    row->setObjectName(QStringLiteral("actionRow"));
    row->setFlat(true);
    row->setCursor(Qt::PointingHandCursor);
    row->setFocusPolicy(Qt::NoFocus);
    auto* icon = new QLabel(row);
    icon->setPixmap(glyphIcon(glyph, accentColor(parent->palette())).pixmap(24, 24));
    auto* name = new QLabel(title, row);
    QFont font = name->font();
    font.setBold(true);
    name->setFont(font);
    auto* note = new QLabel(detail, row);
    note->setForegroundRole(QPalette::PlaceholderText);
    note->setTextFormat(Qt::PlainText);
    auto* text = new QVBoxLayout();
    text->setSpacing(1);
    text->addWidget(name);
    text->addWidget(note);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 7, 12, 7);
    layout->setSpacing(12);
    layout->addWidget(icon);
    layout->addLayout(text, 1);
    for (QLabel* label : {icon, name, note})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->setMinimumHeight(layout->sizeHint().height());
    row->setStyleSheet(QStringLiteral(
        "QPushButton#actionRow { border: none; border-radius: 8px; text-align: left; background: transparent; }"
        "QPushButton#actionRow:hover { background: #29808080; }"
        "QPushButton#actionRow:pressed { background: #47808080; }"));
    QObject::connect(row, &QPushButton::clicked, row, std::move(action));
    return row;
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
    , title_(makeLabel(tr("All Runs"), 1.9, true, this))
    , path_(new QLabel(this))
    , search_(new QLineEdit(this))
    , table_(new QTableView(this))
    , details_(new QLabel(this))
    , openButton_(new QPushButton(tr("Open Run"), this))
    , home_(new QWidget(this))
    , banner_(new FeaturedBanner(this))
    , rows_(new QVBoxLayout())
{
    setObjectName(QStringLiteral("libraryView"));

    // Welcome screen: the app and its actions on the left, and on the right the network every
    // run was streamed over, drawn as a scene at dusk.
    {
        auto* intro = new QWidget(welcome_);
        auto* icon = new QLabel(intro);
        icon->setPixmap(appIcon().pixmap(96, 96));
        auto* name = makeLabel(tr("Welcome to %1").arg(appDisplayName()), 2.1, true, intro);
        name->setWordWrap(true);
        auto* version = new QLabel(tr("Version %1").arg(versionString()), intro);
        version->setForegroundRole(QPalette::PlaceholderText);
        auto* tagline = new QLabel(tr("See what a viewer saw when adaptive video was streamed over a "
                                      "satellite 5G network with integrated access and backhaul (IAB)."),
                                   intro);
        tagline->setWordWrap(true);

        auto* actions = new QVBoxLayout();
        actions->setContentsMargins(-12, 0, 0, 0);  // line the icons up with the text above
        actions->setSpacing(4);
        actions->addWidget(actionRow(Glyph::OpenFolder, tr("Open a Folder of Runs..."),
                                     tr("Lists every run bundle in the folder."),
                                     [this] { emit openFolderRequested(); }, intro));
        actions->addWidget(actionRow(Glyph::OpenBundle, tr("Open a Single Run..."),
                                     tr("Opens one run bundle and replays it."),
                                     [this] { emit openBundleRequested(); }, intro));

        recentBox_->setParent(intro);
        auto* recentLayout = new QVBoxLayout(recentBox_);
        recentLayout->setContentsMargins(-12, 18, 0, 0);
        recentLayout->setSpacing(2);

        auto* column = new QVBoxLayout(intro);
        column->setContentsMargins(56, 40, 48, 40);
        column->addStretch(2);
        column->addWidget(icon);
        column->addSpacing(18);
        column->addWidget(name);
        column->addWidget(version);
        column->addSpacing(14);
        column->addWidget(tagline);
        column->addSpacing(26);
        column->addLayout(actions);
        column->addWidget(recentBox_);
        column->addStretch(3);
        intro->setMaximumWidth(500);
        intro->setMinimumWidth(380);

        auto* row = new QHBoxLayout(welcome_);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        row->addWidget(intro);
        row->addWidget(new WelcomeScene(welcome_), 1);
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
        table_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 10);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(LibraryModel::Video, QHeaderView::Stretch);
        table_->horizontalHeader()->setHighlightSections(false);
        table_->setTextElideMode(Qt::ElideRight);
        table_->setAlternatingRowColors(true);
        table_->setShowGrid(false);
        table_->setFrameShape(QFrame::NoFrame);
        table_->setAttribute(Qt::WA_MacShowFocusRect, false);

        search_->setPlaceholderText(tr("Search"));
        search_->setClearButtonEnabled(true);
        search_->addAction(glyphIcon(Glyph::Search, palette().color(QPalette::PlaceholderText)),
                           QLineEdit::LeadingPosition);
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

        // The home: the featured run and a row of cards for each video, on a dark ground, then
        // the table of every run.
        home_->setObjectName(QStringLiteral("home"));
        home_->setAutoFillBackground(true);
        QPalette dark = home_->palette();
        dark.setColor(QPalette::Window, QColor(0x0b, 0x0d, 0x14));
        dark.setColor(QPalette::WindowText, Qt::white);
        home_->setPalette(dark);
        auto* homeLayout = new QVBoxLayout(home_);
        homeLayout->setContentsMargins(0, 0, 0, 28);
        homeLayout->setSpacing(0);
        homeLayout->addWidget(banner_);
        homeLayout->addLayout(rows_);
        rows_->setContentsMargins(44, 4, 0, 0);
        rows_->setSpacing(22);
        connect(banner_, &FeaturedBanner::playRequested, this, [this] {
            if (banner_->bundle())
                emit openRequested(banner_->bundle()->directory);
        });
        connect(banner_, &FeaturedBanner::infoRequested, this, [this] {
            if (banner_->bundle())
                emit infoRequested(banner_->bundle()->directory);
        });

        auto* list = new QWidget(browser_);
        auto* listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(20, 16, 20, 16);
        listLayout->setSpacing(12);
        listLayout->addLayout(header);
        listLayout->addLayout(body, 1);
        list->setMinimumHeight(420);

        auto* layout = new QVBoxLayout(browser_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(home_);
        layout->addWidget(list, 1);

        connect(table_, &QTableView::activated, this, [this](const QModelIndex&) {
            if (const BundleSummaryPtr bundle = selectedBundle())
                emit openRequested(bundle->directory);
        });
        table_->installEventFilter(this);
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
    path_->setText(tr("%n run(s) in %1", nullptr, static_cast<int>(bundles.size()))
                       .arg(QDir::toNativeSeparators(folder)));
    bundles_ = bundles;
    rebuildHome();
    model_->setBundles(std::move(bundles));
    stack_->setCurrentWidget(browser_);
    if (proxy_->rowCount() > 0 && !table_->currentIndex().isValid())
        table_->selectRow(0);
}

void LibraryView::setScanning(const QString& folder)
{
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
    auto* heading = new QLabel(tr("Recent"), recentBox_);
    QFont font = heading->font();
    font.setBold(true);
    font = format::scaled(font, 0.85);
    heading->setFont(font);
    heading->setForegroundRole(QPalette::PlaceholderText);
    heading->setContentsMargins(10, 0, 0, 4);
    layout->addWidget(heading);
    for (const QString& folder : folders) {
        layout->addWidget(actionRow(Glyph::OpenFolder, QDir(folder).dirName(),
                                    QDir::toNativeSeparators(QDir(folder).absolutePath()),
                                    [this, folder] { emit recentFolderRequested(folder); }, recentBox_));
    }
}

void LibraryView::setThumbnailSource(const QString& ffmpeg, const QString& folder)
{
    delete thumbnails_;
    thumbnails_ = new ThumbnailCache(ffmpeg, folder, this);
    connect(thumbnails_, &ThumbnailCache::ready, this, [this](const QString& directory) {
        const QImage image = thumbnails_->image(directory);
        for (RunCard* card : std::as_const(cards_)) {
            if (card->bundle()->directory == directory)
                card->setImage(image);
        }
        if (banner_->bundle() && banner_->bundle()->directory == directory)
            banner_->setImage(image);
    });
    rebuildHome();
}

void LibraryView::rebuildHome()
{
    while (QLayoutItem* item = rows_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    cards_.clear();
    // One row per video, in the order the videos first appear, its runs by feeder rate.
    QStringList titles;
    QHash<QString, QList<BundleSummaryPtr>> byTitle;
    for (const BundleSummaryPtr& bundle : std::as_const(bundles_)) {
        if (!bundle->errors.isEmpty())
            continue;
        const QString title = bundle->videoTitle.isEmpty() ? bundle->videoName : bundle->videoTitle;
        if (!byTitle.contains(title))
            titles << title;
        byTitle[title] << bundle;
    }
    for (const QString& title : std::as_const(titles)) {
        auto runs = byTitle.value(title);
        std::ranges::stable_sort(runs, {}, [](const BundleSummaryPtr& b) { return b->linkRate.value_or(0); });
        auto* heading = new QLabel(title, home_);
        QFont font = format::scaled(heading->font(), 1.25);
        font.setBold(true);
        heading->setFont(font);
        auto* strip = new QWidget(home_);
        auto* stripLayout = new QHBoxLayout(strip);
        stripLayout->setContentsMargins(0, 0, 0, 0);
        stripLayout->setSpacing(12);
        for (const BundleSummaryPtr& bundle : std::as_const(runs)) {
            auto* card = new RunCard(bundle, strip);
            connect(card, &RunCard::clicked, this, [this, bundle] { feature(bundle); });
            connect(card, &RunCard::opened, this, [this, bundle] { emit openRequested(bundle->directory); });
            stripLayout->addWidget(card);
            cards_ << card;
            if (thumbnails_) {
                card->setImage(thumbnails_->image(bundle->directory));
                // A different moment of the video on each card of the row.
                const double position = 0.25 + 0.2 * static_cast<double>(stripLayout->count() - 1);
                thumbnails_->request(bundle->directory, bundle->videoName, bundle->segments, position);
            }
        }
        stripLayout->addStretch(1);
        auto* section = new QWidget(home_);
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(10);
        sectionLayout->addWidget(heading);
        auto* scroller = new QScrollArea(section);
        scroller->setFrameShape(QFrame::NoFrame);
        scroller->setWidget(strip);
        scroller->setWidgetResizable(true);
        scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroller->setFixedHeight(strip->sizeHint().height() + 14);
        // The row scrolls sideways rather than widening the window to fit every card.
        scroller->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        scroller->setMinimumWidth(1);
        scroller->viewport()->setAutoFillBackground(false);
        strip->setAutoFillBackground(false);
        sectionLayout->addWidget(scroller);
        rows_->addWidget(section);
    }
    // Keep the featured run if it is still listed; otherwise the first run that stalled, or
    // the first run.
    BundleSummaryPtr featured;
    for (RunCard* card : std::as_const(cards_)) {
        if (banner_->bundle() && card->bundle()->directory == banner_->bundle()->directory)
            featured = card->bundle();
    }
    for (RunCard* card : std::as_const(cards_)) {
        if (!featured && card->bundle()->stalls > 0)
            featured = card->bundle();
    }
    if (!featured && !cards_.isEmpty())
        featured = cards_.first()->bundle();
    feature(featured);
    home_->setVisible(!cards_.isEmpty());
}

void LibraryView::feature(const BundleSummaryPtr& bundle)
{
    QString satellite;
    QString backhaul;
    if (bundle) {
        // The numbers of the satellite path, from the header of the run's sim.log.
        const NetworkTopology topology = readNetworkTopology(simLogPath(bundle->directory), -1);
        if (topology.satelliteAltitude)
            satellite = tr("%1 km up").arg(*topology.satelliteAltitude / 1000.0, 0, 'f', 0);
        if (topology.carrierFrequencyGHz)
            backhaul = tr("%1 GHz backhaul").arg(*topology.carrierFrequencyGHz, 0, 'g', 3);
    }
    banner_->setBundle(bundle, satellite, backhaul);
    if (bundle && thumbnails_)
        banner_->setImage(thumbnails_->image(bundle->directory));
    for (RunCard* card : std::as_const(cards_))
        card->setFeatured(bundle && card->bundle()->directory == bundle->directory);
}

bool LibraryView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == table_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (const BundleSummaryPtr bundle = selectedBundle())
                emit openRequested(bundle->directory);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
