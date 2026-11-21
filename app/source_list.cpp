#include "source_list.h"

#include "format.h"
#include "icons.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

namespace abrviz {

namespace {

/**
 * @brief Whether a palette is a dark appearance.
 *
 * @param palette The palette to test.
 * @return True if the window colour's lightness is below 128.
 */
bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

constexpr int kNoteRole = Qt::UserRole + 10;  ///< Item data role holding a run's note.

/**
 * @brief Draws the item as usual, then its note right-aligned in secondary text.
 *
 * The note (stalls, cut-off) is shown the way Mail shows unread counts.
 */
class NoteDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief Paints the item, then its kNoteRole text (if any) in a smaller font.
     *
     * @param painter The painter; its state is restored afterwards.
     * @param option The style options for the item.
     * @param index The item.
     */
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        const QString note = index.data(kNoteRole).toString();
        if (note.isEmpty())
            return;
        painter->save();
        QFont font = option.font;
        font = format::scaled(font, 0.85);
        painter->setFont(font);
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        painter->drawText(option.rect.adjusted(0, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter, note);
        painter->restore();
    }
};

} // namespace

SourceList::SourceList(QWidget* parent)
    : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setColumnCount(1);
    header()->hide();
    setItemDelegate(new NoteDelegate(this));
    setRootIsDecorated(false);
    setIndentation(0);
    setItemsExpandable(false);
    setUniformRowHeights(false);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::NoFocus);
    setIconSize(QSize(16, 16));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAttribute(Qt::WA_MacShowFocusRect, false);

    QTreeWidgetItem* library = addSection(tr("Library"));
    allRuns_ = new QTreeWidgetItem(library, {tr("All Runs")});
    allRuns_->setData(0, KindRole, AllRuns);
    expandAll();

    connect(this, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current) { itemChanged(current); });
    applyStyle();
}

QTreeWidgetItem* SourceList::addSection(const QString& title)
{
    auto* item = new QTreeWidgetItem(this, {title});
    item->setData(0, KindRole, Section);
    item->setFlags(Qt::ItemIsEnabled);  // not selectable
    QFont font = item->font(0);
    font.setBold(true);
    font = format::scaled(font, 0.85);
    item->setFont(0, font);
    item->setSizeHint(0, QSize(0, 30));
    return item;
}

namespace {

/**
 * @brief Tells whether runs come from more than one ABR algorithm.
 *
 * @param runs The runs.
 * @return True if at least two different `algorithm` keys appear.
 */
bool hasSeveralAlgorithms(const QList<BundleSummaryPtr>& runs)
{
    QString first;
    for (const BundleSummaryPtr& run : runs) {
        if (run->algorithm.isEmpty())
            continue;
        if (first.isEmpty())
            first = run->algorithm;
        else if (run->algorithm != first)
            return true;
    }
    return false;
}

} // namespace

void SourceList::contextMenuEvent(QContextMenuEvent* event)
{
    QTreeWidgetItem* item = itemAt(viewport()->mapFrom(this, event->pos()));
    if (!item || item->data(0, KindRole).toInt() != Run || item->isDisabled())
        return;
    const QString directory = item->data(0, DirectoryRole).toString();
    QMenu menu(this);
    menu.addAction(tr("Play"), this, [this, directory] { emit runSelected(directory); });
    menu.addAction(tr("Compare Side by Side"), this, [this, directory] { emit compareRequested(directory); });
    menu.exec(event->globalPos());
}

void SourceList::setRuns(const QList<BundleSummaryPtr>& runs)
{
    const QSignalBlocker block(this);
    while (topLevelItemCount() > 1)
        delete takeTopLevelItem(1);
    highlights_ = nullptr;

    QTreeWidgetItem* section = nullptr;
    QString sectionTitle;
    for (const BundleSummaryPtr& run : runs) {
        const QString link = run->linkRate ? format::bitrate(static_cast<double>(*run->linkRate))
                                           : tr("Uncongested");
        // Runs of several algorithms: one heading per video and rate, one entry per algorithm.
        // Runs of one algorithm only: one heading per video, one entry per rate.
        const bool byAlgorithm = !run->algorithm.isEmpty() && hasSeveralAlgorithms(runs);
        const QString title = byAlgorithm ? tr("%1 · %2").arg(run->videoTitle, link) : run->videoTitle;
        if (!section || title != sectionTitle) {
            sectionTitle = title;
            section = addSection(sectionTitle);
        }
        auto* item = new QTreeWidgetItem(section, {byAlgorithm ? algorithmDisplayName(run->algorithm) : link});
        item->setData(0, KindRole, Run);
        item->setData(0, DirectoryRole, run->directory);
        item->setToolTip(0, run->runId);
        // A short note on the right, as Mail shows counts: stalls, or a cut-off.
        QString note;
        // Rebuffering episodes, not the player's single-frame pauses.
        if (run->stallEpisodes == 1)
            note = tr("1 stall");
        else if (run->stallEpisodes > 1)
            note = tr("%1 stalls").arg(run->stallEpisodes);
        else if (run->cutOff)
            note = tr("cut off");
        item->setData(0, kNoteRole, note);
        if (!run->errors.isEmpty()) {
            item->setDisabled(true);
            item->setToolTip(0, run->errors.join(QLatin1Char('\n')));
        }
    }
    expandAll();
    applyStyle();
}

void SourceList::setHighlights(const std::vector<Highlight>& highlights, const QList<BundleSummaryPtr>& runs)
{
    const QSignalBlocker block(this);
    delete highlights_;
    highlights_ = nullptr;
    if (highlights.empty())
        return;
    // Above the videos, right under All Runs.
    highlights_ = new QTreeWidgetItem({tr("Featured Comparisons")});
    highlights_->setData(0, KindRole, Section);
    highlights_->setFlags(Qt::ItemIsEnabled);
    QFont font = highlights_->font(0);
    font.setBold(true);
    highlights_->setFont(0, format::scaled(font, 0.85));
    highlights_->setSizeHint(0, QSize(0, 30));
    insertTopLevelItem(1, highlights_);
    for (const Highlight& h : highlights) {
        const auto it = std::ranges::find(runs, h.directory, [](const BundleSummaryPtr& r) { return r->directory; });
        if (it == runs.end())
            continue;
        const BundleSummary& run = **it;
        const QString rate = run.linkRate ? format::bitrate(static_cast<double>(*run.linkRate)) : tr("uncongested");
        // The rate first: long video titles are cut at the end, and it tells two runs of the
        // same video apart.
        auto* item = new QTreeWidgetItem(highlights_, {tr("%1 · %2").arg(rate, run.videoTitle)});
        item->setData(0, KindRole, HighlightRun);
        item->setData(0, DirectoryRole, h.directory);
        item->setData(0, RivalRole, h.rivalDirectory);
        item->setData(0, TimeRole, h.time);
        // No note on the right: the names are long. The numbers are in the tool tip.
        item->setToolTip(0, tr("%1\nVMAF %2 against %3 for the next best algorithm.\nOpens LLM next to %4, "
                               "lined up by frame, where its picture is furthest ahead (VMAF %5 against %6).")
                                .arg(item->text(0))
                                .arg(h.vmaf, 0, 'f', 1)
                                .arg(h.bestOtherVmaf, 0, 'f', 1)
                                .arg(algorithmDisplayName(h.rivalAlgorithm))
                                .arg(h.momentVmaf, 0, 'f', 0)
                                .arg(h.rivalMomentVmaf, 0, 'f', 0));
    }
    highlights_->setExpanded(true);
    applyStyle();
}

int SourceList::highlightCount() const
{
    return highlights_ ? highlights_->childCount() : 0;
}

QTreeWidgetItem* SourceList::findRun(const QString& directory) const
{
    for (int s = 1; s < topLevelItemCount(); ++s) {
        QTreeWidgetItem* section = topLevelItem(s);
        if (section == highlights_)
            continue;
        for (int r = 0; r < section->childCount(); ++r) {
            if (section->child(r)->data(0, DirectoryRole).toString() == directory)
                return section->child(r);
        }
    }
    return nullptr;
}

void SourceList::selectRun(const QString& directory)
{
    QTreeWidgetItem* item = directory.isEmpty() ? allRuns_ : findRun(directory);
    if (!item)
        return;
    selecting_ = true;
    setCurrentItem(item);
    selecting_ = false;
}

void SourceList::selectAllRuns()
{
    selectRun(QString());
}

int SourceList::runCount() const
{
    int count = 0;
    for (int s = 1; s < topLevelItemCount(); ++s) {
        if (topLevelItem(s) != highlights_)
            count += topLevelItem(s)->childCount();
    }
    return count;
}

QString SourceList::runLabel(const QString& directory) const
{
    const QTreeWidgetItem* item = findRun(directory);
    return item ? QStringLiteral("%1 / %2").arg(item->parent()->text(0), item->text(0)) : QString();
}

void SourceList::itemChanged(QTreeWidgetItem* current)
{
    if (selecting_ || !current)
        return;
    switch (current->data(0, KindRole).toInt()) {
    case AllRuns:
        emit allRunsSelected();
        break;
    case HighlightRun:
        emit compareAtRequested(current->data(0, DirectoryRole).toString(),
                                current->data(0, RivalRole).toString(), current->data(0, TimeRole).toDouble());
        break;
    case Run:
        emit runSelected(current->data(0, DirectoryRole).toString());
        break;
    default:
        break;
    }
}

void SourceList::changeEvent(QEvent* event)
{
    QTreeWidget::changeEvent(event);
    // Setting a style sheet changes the palette too, so the restyle must not re-enter itself.
    if (event->type() == QEvent::PaletteChange && !restyling_) {
        restyling_ = true;
        applyStyle();
        restyling_ = false;
    }
}

void SourceList::applyStyle()
{
    const bool dark = isDark(palette());
    const QColor text = palette().color(QPalette::WindowText);
    const QColor secondary = palette().color(QPalette::PlaceholderText);
    const QColor accent = accentColor(palette());
    // Section headings are secondary text; run icons take the accent colour, as in Finder.
    for (int s = 0; s < topLevelItemCount(); ++s) {
        QTreeWidgetItem* section = topLevelItem(s);
        section->setForeground(0, secondary);
        for (int r = 0; r < section->childCount(); ++r) {
            QTreeWidgetItem* item = section->child(r);
            const bool all = item->data(0, KindRole).toInt() == AllRuns;
            item->setIcon(0, glyphIcon(all ? Glyph::Library : Glyph::Film, accent, accent));
            item->setSizeHint(0, QSize(0, 28));
        }
    }
    // Matches the sidebar column behind it: darker than the window in the dark theme.
    const QColor background = dark ? palette().color(QPalette::Window).darker(150) : QColor(0xec, 0xec, 0xee);
    QColor selection = dark ? accent : QColor(0, 0, 0, 22);
    if (dark)
        selection.setAlphaF(0.30F);
    setStyleSheet(QStringLiteral(R"(
        QTreeWidget#sidebar { background: %1; border: none; font-size: 13px; padding: 0 8px; }
        QTreeWidget#sidebar::item { border: none; border-radius: 6px; padding-left: 6px; color: %2; }
        QTreeWidget#sidebar::item:selected { background: %3; color: %2; }
        QTreeWidget#sidebar::item:disabled { color: %4; }
    )")
                      .arg(background.name(),
                           text.name(),
                           selection.name(QColor::HexArgb),
                           secondary.name()));
}

} // namespace abrviz
