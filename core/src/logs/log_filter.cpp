#include "abrviz/core/logs/log_filter.h"

#include <QPromise>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace abrviz {

namespace {

constexpr qsizetype kStopCheckInterval = 2048;  ///< Lines between two calls of the stop callback.

/**
 * @brief One word or quoted phrase of the filter text.
 */
struct Term {
    QByteArray bytes;            ///< UTF-8 text to look for, without quotes or the leading `-`.
    bool exclude = false;        ///< Lines containing the term are dropped (leading `-`).
    bool caseSensitive = false;  ///< The term has an upper-case letter, so case matters.
};

/**
 * @brief Splits the filter text into words and quoted phrases.
 *
 * A leading `-` marks a term as excluded. A term with an upper-case letter is case-sensitive.
 * An unclosed quote runs to the end of the text. Empty terms are dropped.
 *
 * @param text The filter text.
 * @return The terms in order.
 */
std::vector<Term> parseTerms(const QString& text)
{
    std::vector<Term> terms;
    const QByteArray utf8 = text.toUtf8();
    qsizetype i = 0;
    while (i < utf8.size()) {
        while (i < utf8.size() && utf8[i] == ' ')
            ++i;
        if (i >= utf8.size())
            break;
        Term term;
        if (utf8[i] == '-' && i + 1 < utf8.size() && utf8[i + 1] != ' ') {
            term.exclude = true;
            ++i;
        }
        qsizetype end = i;
        if (utf8[i] == '"') {
            ++i;
            end = utf8.indexOf('"', i);
            if (end < 0)
                end = utf8.size();
            term.bytes = utf8.sliced(i, end - i);
            i = std::min(end + 1, utf8.size());
        } else {
            while (end < utf8.size() && utf8[end] != ' ')
                ++end;
            term.bytes = utf8.sliced(i, end - i);
            i = end;
        }
        if (term.bytes.isEmpty())
            continue;
        term.caseSensitive = term.bytes != term.bytes.toLower();
        terms.push_back(std::move(term));
    }
    return terms;
}

} // namespace

LogRows filterLog(const LogIndex& index, const LogFilterSpec& spec,
                  const std::function<bool()>& stop)
{
    const std::vector<Term> terms = parseTerms(spec.text);
    LogRows rows;
    const auto lines = index.lines();
    for (qsizetype row = 0; row < static_cast<qsizetype>(lines.size()); ++row) {
        if (stop && row % kStopCheckInterval == 0 && stop())
            return rows;
        const LogLine& line = lines[static_cast<std::size_t>(row)];
        if (line.level() < spec.minLevel)
            continue;
        if (spec.ue != LogFilterSpec::kAllUes && line.ue >= 0 && line.ue != spec.ue)
            continue;
        const bool matches = std::ranges::all_of(terms, [&](const Term& term) {
            const QByteArrayView text = term.caseSensitive ? index.text(line)
                                                           : index.lowerText(line);
            return text.contains(term.bytes) != term.exclude;
        });
        if (matches)
            rows.push_back(static_cast<qint32>(row));
    }
    return rows;
}

LogFilter::LogFilter()
    : generation_(std::make_shared<std::atomic<quint64>>(0))
{
}

QFuture<LogFilterResult> LogFilter::request(LogIndexPtr index, LogFilterSpec spec,
                                            QThreadPool* pool)
{
    const quint64 generation = generation_->fetch_add(1, std::memory_order_acq_rel) + 1;
    if (!pool)
        pool = QThreadPool::globalInstance();
    return QtConcurrent::run(
        pool, [index = std::move(index), spec = std::move(spec), generation,
               counter = generation_](QPromise<LogFilterResult>& promise) {
            auto stop = [&] {
                return promise.isCanceled()
                    || counter->load(std::memory_order_acquire) != generation;
            };
            if (!index || stop())
                return;
            LogRows rows = filterLog(*index, spec, stop);
            if (stop())
                return;  // incomplete; a newer request or a cancellation replaces it
            promise.addResult(LogFilterResult{generation, std::move(rows)});
        });
}

void LogFilter::invalidate()
{
    generation_->fetch_add(1, std::memory_order_acq_rel);
}

bool LogFilter::isCurrent(quint64 generation) const
{
    return generation_->load(std::memory_order_acquire) == generation;
}

quint64 LogFilter::generation() const
{
    return generation_->load(std::memory_order_acquire);
}

} // namespace abrviz
