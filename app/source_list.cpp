#include "source_list.h"

#include "format.h"
#include "icons.h"

#include <QEvent>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

namespace abrviz {

namespace {

bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

constexpr int kNoteRole = Qt::UserRole + 10;

/// Draws the item as usual, then its note (stalls, cut-off) right-aligned in secondary text,
/// the way Mail shows unread counts.
class NoteDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

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

void SourceList::setRuns(const QList<BundleSummaryPtr>& runs)
{
    const QSignalBlocker block(this);
    while (topLevelItemCount() > 1)
        delete takeTopLevelItem(1);

    QTreeWidgetItem* section = nullptr;
    QString sectionTitle;
    for (const BundleSummaryPtr& run : runs) {
        if (!section || run->videoTitle != sectionTitle) {
            sectionTitle = run->videoTitle;
            section = addSection(sectionTitle);
        }
        const QString link = run->linkRate ? format::bitrate(static_cast<double>(*run->linkRate))
                                           : tr("Uncongested");
        auto* item = new QTreeWidgetItem(section, {link});
        item->setData(0, KindRole, Run);
        item->setData(0, DirectoryRole, run->directory);
        item->setToolTip(0, run->runId);
        // A short note on the right, as Mail shows counts: stalls, or a cut-off.
        QString note;
        if (run->stalls == 1)
            note = tr("1 stall");
        else if (run->stalls > 1)
            note = tr("%1 stalls").arg(run->stalls);
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

QTreeWidgetItem* SourceList::findRun(const QString& directory) const
{
    for (int s = 1; s < topLevelItemCount(); ++s) {
        QTreeWidgetItem* section = topLevelItem(s);
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
    for (int s = 1; s < topLevelItemCount(); ++s)
        count += topLevelItem(s)->childCount();
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
    const QColor accent = palette().color(QPalette::Active, QPalette::Highlight);
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
    const QColor background = dark ? QColor(0x26, 0x26, 0x28) : QColor(0xec, 0xec, 0xee);
    const QColor selection = dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 22);
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
