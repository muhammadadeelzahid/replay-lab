#include "log_console.h"

#include "session.h"
#include "time_cursor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

namespace abrviz {

namespace {

constexpr int kDebounceMs = 150;

const QColor kWarning(0xc0, 0x39, 0x2b);

} // namespace

// ---- LogModel ----

void LogModel::setIndex(LogIndexPtr index)
{
    beginResetModel();
    index_ = std::move(index);
    rows_.clear();
    endResetModel();
}

void LogModel::setRows(LogRows rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const LogLine* LogModel::lineAt(int row) const
{
    if (!index_ || row < 0 || row >= static_cast<int>(rows_.size()))
        return nullptr;
    return &index_->line(rows_[static_cast<std::size_t>(row)]);
}

int LogModel::rowAtTime(double time) const
{
    if (!index_ || rows_.empty())
        return -1;
    const auto it = std::ranges::upper_bound(rows_, time, {}, [this](qint32 row) {
        return index_->line(row).time;
    });
    return static_cast<int>(it - rows_.begin()) - 1;
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int LogModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    const LogLine* line = lineAt(index.row());
    if (!line)
        return {};
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Time:
            return line->kind == LogKind::Setup ? QString()
                                                : QString::number(line->time, 'f', 3);
        case Ue:
            return line->ue >= 0 ? QString::number(line->ue) : QString();
        case Source:
            return QStringLiteral("%1:%2")
                .arg(line->source == LogSource::Sim ? QStringLiteral("sim")
                                                    : QStringLiteral("policy"))
                .arg(line->lineNumber);
        case Text:
            return index_->displayText(*line);
        default:
            return {};
        }
    case Qt::ToolTipRole:
        return index.column() == Text ? index_->displayText(*line) : kindName(line->kind);
    case Qt::ForegroundRole:
        if (line->level() == LogLevel::Warning)
            return kWarning;
        if (line->level() == LogLevel::Debug)
            return QColor(Qt::gray);
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == Time || index.column() == Ue)
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    default:
        return {};
    }
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Time: return tr("Time (s)");
    case Ue: return tr("UE");
    case Source: return tr("Line");
    case Text: return tr("Text");
    default: return {};
    }
}

// ---- LogConsole ----

LogConsole::LogConsole(Session* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , watcher_(new QFutureWatcher<LogFilterResult>(this))
    , debounce_(new QTimer(this))
    , model_(new LogModel(this))
    , text_(new QLineEdit(this))
    , ue_(new QComboBox(this))
    , level_(new QComboBox(this))
    , follow_(new QCheckBox(tr("Follow cursor"), this))
    , status_(new QLabel(this))
    , table_(new QTableView(this))
{
    setObjectName(QStringLiteral("logConsole"));

    text_->setPlaceholderText(tr("Filter: words, \"a phrase\", -exclude"));
    text_->setClearButtonEnabled(true);
    level_->addItem(tr("Warnings"), QVariant::fromValue(static_cast<int>(LogLevel::Warning)));
    level_->addItem(tr("Info"), QVariant::fromValue(static_cast<int>(LogLevel::Info)));
    level_->addItem(tr("All, with frames"), QVariant::fromValue(static_cast<int>(LogLevel::Debug)));
    level_->setCurrentIndex(1);
    ue_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    follow_->setChecked(true);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    table_->setModel(model_);
    table_->setFont(mono);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Fixed row heights keep large tables fast: no per-row size queries.
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(QFontMetrics(mono).height() + 4);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    const int digit = QFontMetrics(mono).horizontalAdvance(QLatin1Char('0'));
    table_->setColumnWidth(LogModel::Time, digit * 10);
    table_->setColumnWidth(LogModel::Ue, digit * 4);
    table_->setColumnWidth(LogModel::Source, digit * 12);

    auto* controls = new QHBoxLayout();
    controls->addWidget(text_, 1);
    controls->addWidget(ue_);
    controls->addWidget(level_);
    controls->addWidget(follow_);
    controls->addWidget(status_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(controls);
    layout->addWidget(table_);

    debounce_->setSingleShot(true);
    debounce_->setInterval(kDebounceMs);
    connect(debounce_, &QTimer::timeout, this, &LogConsole::requestFilter);
    connect(text_, &QLineEdit::textChanged, this, [this] {
        debounce_->start();
        updateStatus();
    });
    connect(ue_, &QComboBox::currentIndexChanged, this, &LogConsole::requestFilter);
    connect(level_, &QComboBox::currentIndexChanged, this, &LogConsole::requestFilter);
    connect(follow_, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            followCursor(session_->cursor()->time());
    });
    connect(watcher_, &QFutureWatcherBase::finished, this, &LogConsole::filterFinished);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &LogConsole::rowSelected);

    connect(session_, &Session::runChanged, this, &LogConsole::indexChanged);
    connect(session_, &Session::logIndexChanged, this, &LogConsole::indexChanged);
    connect(session_->cursor(), &TimeCursor::timeChanged, this,
            [this](double time, TimeCursor::Source source) {
                if (source != TimeCursor::Source::Logs && follow_->isChecked())
                    followCursor(time);
            });
    indexChanged();
}

LogConsole::~LogConsole()
{
    // No result may arrive while the members are destroyed; the guards then wait for the tasks.
    watcher_->disconnect(this);
    current_.cancel();
}

LogFilterSpec LogConsole::spec() const
{
    LogFilterSpec spec;
    spec.text = text_->text();
    spec.ue = ue_->currentData().isValid() ? ue_->currentData().toInt() : LogFilterSpec::kAllUes;
    spec.minLevel = static_cast<LogLevel>(level_->currentData().toInt());
    return spec;
}

bool LogConsole::isFiltering() const
{
    return debounce_->isActive() || pending_;
}

void LogConsole::setFollowCursor(bool follow)
{
    follow_->setChecked(follow);
}

void LogConsole::indexChanged()
{
    if (session_->logIndex() == index_ && index_)
        return;
    index_ = session_->logIndex();
    filter_.invalidate();
    current_.cancel();
    retiring_.push_back(std::move(current_));
    model_->setIndex(index_);
    pending_ = false;

    // UE choices: all, or one UE (with the lines that belong to no UE).
    const QSignalBlocker blocker(ue_);
    ue_->clear();
    ue_->addItem(tr("All UEs"), LogFilterSpec::kAllUes);
    if (index_) {
        for (const int ue : index_->ues()) {
            ue_->addItem(ue == index_->nodeId() ? tr("UE %1 (this bundle)").arg(ue)
                                                : tr("UE %1").arg(ue),
                         ue);
        }
        ue_->setCurrentIndex(std::max(0, ue_->findData(index_->nodeId())));
        requestFilter();
    }
    updateStatus();
}

void LogConsole::requestFilter()
{
    debounce_->stop();
    if (!index_)
        return;
    // Finished tasks need no guard; the others are cancelled and left to finish on their own.
    std::erase_if(retiring_, [](const FutureGuard<LogFilterResult>& g) { return !g.isRunning(); });
    current_.cancel();
    retiring_.push_back(std::move(current_));

    auto future = filter_.request(index_, spec());
    current_ = FutureGuard<LogFilterResult>(future);
    pending_ = true;
    watcher_->setFuture(future);
    updateStatus();
}

void LogConsole::filterFinished()
{
    const auto future = watcher_->future();
    if (future.isCanceled() || future.resultCount() == 0) {
        // Stopped: the watcher only watches the newest request, so nothing else is coming.
        pending_ = false;
        updateStatus();
        return;
    }
    LogFilterResult result = future.result();
    if (!filter_.isCurrent(result.generation)) {
        ++discarded_;
        return;
    }
    std::ignore = current_.release();
    pending_ = false;
    model_->setRows(std::move(result.rows));
    updateStatus();
    emit filterApplied(result.generation);
    if (follow_->isChecked())
        followCursor(session_->cursor()->time());
}

void LogConsole::followCursor(double time)
{
    const int row = model_->rowAtTime(time);
    if (row < 0)
        return;
    selecting_ = true;
    const QModelIndex index = model_->index(row, LogModel::Text);
    table_->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    table_->scrollTo(index, QAbstractItemView::PositionAtCenter);
    selecting_ = false;
}

void LogConsole::rowSelected(const QModelIndex& current)
{
    if (selecting_ || !current.isValid())
        return;
    if (const LogLine* line = model_->lineAt(current.row()); line && line->kind != LogKind::Setup)
        session_->cursor()->setTime(line->time, TimeCursor::Source::Logs);
}

void LogConsole::updateStatus()
{
    if (!session_->run()) {
        status_->setText(tr("No run open"));
        return;
    }
    if (!index_) {
        status_->setText(tr("Reading logs ..."));
        return;
    }
    const QLocale locale;
    QString text = tr("%1 of %2 lines")
                       .arg(locale.toString(static_cast<qlonglong>(model_->rows().size())),
                            locale.toString(static_cast<qlonglong>(index_->size())));
    if (isFiltering())
        text += tr(" (filtering)");
    status_->setText(text);
}

} // namespace abrviz
