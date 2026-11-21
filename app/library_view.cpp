#include "library_view.h"

#include "branding.h"
#include "format.h"
#include "home_widgets.h"
#include "icons.h"
#include "thumbnail_cache.h"

#include "abrviz/core/io/topology_reader.h"
#include "abrviz/core/load/bundle_reader.h"

#include "abrviz/core/version.h"

#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
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
    if (role == Qt::TextAlignmentRole && index.column() != Video && index.column() != Algorithm)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case Video:
        return bundle->videoTitle.isEmpty() ? bundle->videoName : bundle->videoTitle;
    case Algorithm:
        return algorithmDisplayName(bundle->algorithm);
    case LinkRate:
        return bundle->linkRate ? format::bitrate(static_cast<double>(*bundle->linkRate))
                                : tr("uncongested");
    case Segments:
        return bundle->segments;
    case Stalls:
        return bundle->stallEpisodes;
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
    case Algorithm: return tr("ABR");
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

    // Welcome screen: a title, three cards for the ways to get runs, and the recent folders.
    {
        auto* page = new GlowBackground(welcome_);
        auto* title = makeLabel(tr("Open Simulation Runs"), 2.4, true, page);
        title->setAlignment(Qt::AlignCenter);
        auto* subtitle = new QLabel(tr("Compare how ABR algorithms streamed video over a satellite 5G network, "
                                       "and replay what each viewer saw."),
                                    page);
        subtitle->setFont(format::scaled(subtitle->font(), 1.15));
        subtitle->setForegroundRole(QPalette::PlaceholderText);
        subtitle->setAlignment(Qt::AlignCenter);
        subtitle->setWordWrap(true);

        auto* folderCard = new SourceCard(Glyph::OpenFolder, tr("Open a Folder"),
                                          tr("Browse every run bundle in a folder"), true, page);
        auto* runCard = new SourceCard(Glyph::OpenBundle, tr("Open a Single Run"),
                                       tr("Replay one run bundle on its own"), false, page);
        auto* getCard = new SourceCard(Glyph::Download, tr("Get the Simulation Runs"),
                                       tr("Download viewer_bundles.tar from the releases page"), false, page);
        folderCard->setObjectName(QStringLiteral("actionRow"));
        runCard->setObjectName(QStringLiteral("actionRow"));
        getCard->setObjectName(QStringLiteral("downloadCard"));
        connect(folderCard, &QPushButton::clicked, this, &LibraryView::openFolderRequested);
        connect(runCard, &QPushButton::clicked, this, &LibraryView::openBundleRequested);
        connect(getCard, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/muhammadadeelzahid/replay-lab/releases")));
        });
        auto* cards = new QHBoxLayout();
        cards->setSpacing(10);
        for (SourceCard* card : {folderCard, runCard, getCard})
            cards->addWidget(card);

        recentBox_->setParent(page);
        auto* recentLayout = new QVBoxLayout(recentBox_);
        recentLayout->setContentsMargins(6, 0, 6, 0);
        recentLayout->setSpacing(12);

        auto* content = new QWidget(page);
        content->setMaximumWidth(1000);
        auto* column = new QVBoxLayout(content);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        column->addWidget(title);
        column->addSpacing(8);
        column->addWidget(subtitle);
        column->addSpacing(34);
        column->addLayout(cards);
        column->addSpacing(34);
        column->addWidget(recentBox_);

        auto* version = new QLabel(tr("%1 %2").arg(appDisplayName(), versionString()), page);
        version->setForegroundRole(QPalette::PlaceholderText);
        version->setAlignment(Qt::AlignCenter);

        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(40, 40, 40, 24);
        outer->addStretch(2);
        outer->addWidget(content, 0, Qt::AlignHCenter);
        outer->addStretch(3);
        outer->addWidget(version);
        content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto* row = new QHBoxLayout(welcome_);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(page);
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
        card->setFixedWidth(360);
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
        // The featured run is a dark card, like a poster, in both appearances; the rows below
        // it follow the window's colours.
        home_->setObjectName(QStringLiteral("home"));
        auto* homeLayout = new QVBoxLayout(home_);
        homeLayout->setContentsMargins(20, 20, 20, 28);
        homeLayout->setSpacing(18);
        homeLayout->addWidget(banner_);
        homeLayout->addLayout(rows_);
        rows_->setContentsMargins(24, 4, 0, 0);
        rows_->setSpacing(22);
        connect(banner_, &FeaturedBanner::playRequested, this, [this] {
            if (banner_->bundle())
                emit openRequested(banner_->bundle()->directory);
        });
        connect(banner_, &FeaturedBanner::compareRequested, this, [this] {
            if (!banner_->bundle())
                return;
            // A highlight opens next to its rival at its moment.
            if (const Highlight* h = highlightOf(banner_->bundle()->directory))
                emit compareAtRequested(h->directory, h->rivalDirectory, h->time);
            else
                emit compareRequested(banner_->bundle()->directory);
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
    auto* heading = new QLabel(tr("Recently Opened"), recentBox_);
    QFont font = heading->font();
    font.setBold(true);
    font = format::scaled(font, 1.25);
    heading->setFont(font);
    layout->addWidget(heading);
    // The folders as wide rows, two to a line, newest first.
    auto* strip = new QWidget(recentBox_);
    auto* grid = new QGridLayout(strip);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);
    for (int i = 0; i < folders.size(); ++i) {
        const QString& folder = folders[i];
        QPushButton* card = actionRow(Glyph::OpenFolder, QDir(folder).dirName(),
                                      QDir::toNativeSeparators(QDir(folder).absolutePath()),
                                      [this, folder] { emit recentFolderRequested(folder); }, strip);
        // A faint tint of the text colour, so the rows work in light and dark mode.
        const QColor ink = palette().color(QPalette::WindowText);
        const auto tint = [&ink](int alpha) {
            return QStringLiteral("rgba(%1,%2,%3,%4)").arg(ink.red()).arg(ink.green()).arg(ink.blue()).arg(alpha);
        };
        card->setStyleSheet(QStringLiteral(
            "QPushButton#actionRow { border: none; border-radius: 10px; text-align: left; background: %1; }"
            "QPushButton#actionRow:hover { background: %2; }"
            "QPushButton#actionRow:pressed { background: %3; }").arg(tint(14), tint(26), tint(8)));
        grid->addWidget(card, i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    layout->addWidget(strip);
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
    // A row of cards under a heading; `caption` may relabel each card.
    const auto addRow = [this](const QString& heading, const QList<BundleSummaryPtr>& runs,
                               const std::function<void(RunCard*, const BundleSummaryPtr&)>& setUp) {
        auto* label = new QLabel(heading, home_);
        QFont font = format::scaled(label->font(), 1.25);
        font.setBold(true);
        label->setFont(font);
        auto* strip = new QWidget(home_);
        auto* stripLayout = new QHBoxLayout(strip);
        stripLayout->setContentsMargins(0, 0, 0, 0);
        stripLayout->setSpacing(12);
        for (const BundleSummaryPtr& bundle : runs) {
            auto* card = new RunCard(bundle, strip);
            connect(card, &RunCard::clicked, this, [this, bundle] { feature(bundle); });
            setUp(card, bundle);
            stripLayout->addWidget(card);
            cards_ << card;
            if (thumbnails_) {
                card->setImage(thumbnails_->image(bundle->directory));
                // A different moment of the video on each card of the row.
                const double position = 0.25 + 0.2 * static_cast<double>((stripLayout->count() - 1) % 4);
                thumbnails_->request(bundle->directory, bundle->videoName, bundle->segments, position);
            }
        }
        stripLayout->addStretch(1);
        auto* section = new QWidget(home_);
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(10);
        sectionLayout->addWidget(label);
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
    };

    // First, the experiments where the LLM looks better; a card opens it next to its rival at
    // the moment that shows it best.
    QList<BundleSummaryPtr> highlighted;
    for (const Highlight& h : highlights_) {
        for (const BundleSummaryPtr& bundle : std::as_const(bundles_)) {
            if (bundle->directory == h.directory && bundle->errors.isEmpty())
                highlighted << bundle;
        }
    }
    if (!highlighted.isEmpty()) {
        addRow(tr("Where the LLM looks better"), highlighted, [this](RunCard* card, const BundleSummaryPtr& bundle) {
            const Highlight* h = highlightOf(bundle->directory);
            const QString rate = bundle->linkRate ? format::bitrate(static_cast<double>(*bundle->linkRate)) : tr("uncongested");
            card->setCaption(bundle->videoTitle.isEmpty() ? bundle->videoName : bundle->videoTitle,
                             tr("%1 · VMAF %2, next best %3").arg(rate).arg(h->vmaf, 0, 'f', 1).arg(h->bestOtherVmaf, 0, 'f', 1));
            card->setToolTip(tr("LLM against %1 at %2: VMAF %3 against %4.\nDouble-click to compare them side by side.")
                                 .arg(algorithmDisplayName(h->rivalAlgorithm), tr("%1 s").arg(h->time, 0, 'f', 0))
                                 .arg(h->momentVmaf, 0, 'f', 0)
                                 .arg(h->rivalMomentVmaf, 0, 'f', 0));
            const Highlight copy = *h;
            const auto compare = [this, copy] { emit compareAtRequested(copy.directory, copy.rivalDirectory, copy.time); };
            connect(card, &RunCard::opened, this, compare);
            connect(card, &RunCard::compareRequested, this, compare);
        });
    }

    // Then one row per video, in the order the videos first appear, its runs by feeder rate.
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
        // By rate and run, the algorithms of each in the usual order (LLM first).
        std::ranges::stable_sort(runs, [](const BundleSummaryPtr& a, const BundleSummaryPtr& b) {
            if (a->linkRate.value_or(0) != b->linkRate.value_or(0))
                return a->linkRate.value_or(0) < b->linkRate.value_or(0);
            if (a->runNumber != b->runNumber)
                return a->runNumber < b->runNumber;
            return algorithmRank(a->algorithm) < algorithmRank(b->algorithm);
        });
        addRow(title, runs, [this](RunCard* card, const BundleSummaryPtr& bundle) {
            connect(card, &RunCard::opened, this, [this, bundle] { emit openRequested(bundle->directory); });
            connect(card, &RunCard::compareRequested, this, [this, bundle] { emit compareRequested(bundle->directory); });
        });
    }
    // Keep the featured run if it is still listed; otherwise the first run that stalled, or
    // the first run.
    BundleSummaryPtr featured;
    for (RunCard* card : std::as_const(cards_)) {
        if (banner_->bundle() && card->bundle()->directory == banner_->bundle()->directory)
            featured = card->bundle();
    }
    if (!featured && !highlighted.isEmpty())
        featured = highlighted.front();
    for (RunCard* card : std::as_const(cards_)) {
        if (!featured && card->bundle()->stallEpisodes > 0)
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
    // The other algorithms of the same video, rate and run number.
    QList<BundleSummaryPtr> experiment;
    if (bundle) {
        for (const Experiment& e : groupExperiments(bundles_)) {
            if (std::ranges::any_of(e.runs, [&](const BundleSummaryPtr& r) { return r->directory == bundle->directory; }))
                experiment = e.runs;
        }
    }
    banner_->setExperiment(experiment);
    if (const Highlight* h = bundle ? highlightOf(bundle->directory) : nullptr) {
        banner_->setHighlightText(
            tr("Best picture of the four: VMAF %1, next %2%3")
                .arg(h->vmaf, 0, 'f', 1)
                .arg(h->bestOtherVmaf, 0, 'f', 1)
                .arg(h->stalledTime > 0.0 ? QString() : tr(", no stalls")));
    }
    if (bundle && thumbnails_)
        banner_->setImage(thumbnails_->image(bundle->directory));
    for (RunCard* card : std::as_const(cards_))
        card->setFeatured(bundle && card->bundle()->directory == bundle->directory);
}

void LibraryView::setHighlights(std::vector<Highlight> highlights)
{
    highlights_ = std::move(highlights);
    rebuildHome();
    // The clearest one is featured.
    for (RunCard* card : std::as_const(cards_)) {
        if (!highlights_.empty() && card->bundle()->directory == highlights_.front().directory) {
            feature(card->bundle());
            break;
        }
    }
}

const Highlight* LibraryView::highlightOf(const QString& directory) const
{
    const auto it = std::ranges::find(highlights_, directory, &Highlight::directory);
    return it == highlights_.end() ? nullptr : &*it;
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
    html += row(tr("Stalls"), bundle->stallEpisodes == 0
                                  ? tr("none")
                                  : QStringLiteral("<b style='color:#d0453a'>%1</b>").arg(bundle->stallEpisodes));
    html += row(tr("Playback"), bundle->cutOff ? tr("cut off by the end of the simulation")
                                               : tr("played to the end"));
    html += row(tr("Log match"), QStringLiteral("%1 %").arg(bundle->logDecisionAgreement * 100.0, 0, 'f', 0));
    html += QStringLiteral("</table>");
    for (const QString& error : bundle->errors)
        html += QStringLiteral("<p style='color:#d0453a'>%1</p>").arg(error.toHtmlEscaped());
    details_->setText(html);
}

} // namespace abrviz
