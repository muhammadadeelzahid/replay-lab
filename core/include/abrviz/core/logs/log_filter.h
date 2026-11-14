#pragma once

/**
 * @file
 * @brief Text, UE and level filtering of the log index, synchronously or on a thread pool.
 */

#include "abrviz/core/logs/log_index.h"

#include <QFuture>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class QThreadPool;

namespace abrviz {

/**
 * @brief What the log console shows.
 *
 * `text` holds words separated by spaces; a line must contain every word. `"two words"`
 * matches a phrase, and `-word` excludes lines containing it. Matching ignores case unless the
 * word has an upper-case letter.
 */
struct LogFilterSpec {
    static constexpr int kAllUes = -2;  ///< Value of `ue` that keeps the lines of every UE.

    QString text;  ///< words and phrases to match; empty keeps every line
    /// A node id, or `kAllUes`. Choosing a UE keeps the lines that belong to no UE (setup,
    /// progress), which give the context of that UE's lines.
    int ue = kAllUes;
    LogLevel minLevel = LogLevel::Info;  ///< Lines below this level are dropped.

    /**
     * @brief Compares two specs member by member.
     *
     * @return True if text, UE and level are all equal.
     */
    friend bool operator==(const LogFilterSpec&, const LogFilterSpec&) = default;
};

/** @brief Rows of the index that match, in index order. */
using LogRows = std::vector<qint32>;

/**
 * @brief Filters `index` synchronously.
 *
 * @param index The log index.
 * @param spec The filter.
 * @param stop Polled every few thousand lines; when it returns true the function returns early
 *             with an incomplete result. May be empty.
 * @return The matching rows in index order.
 */
[[nodiscard]] LogRows filterLog(const LogIndex& index, const LogFilterSpec& spec,
                                const std::function<bool()>& stop = {});

/**
 * @brief The rows matching one filter request, tagged with the request's generation.
 */
struct LogFilterResult {
    quint64 generation = 0;  ///< generation of the request that produced the rows
    LogRows rows;            ///< matching rows of the index, in index order
};

/**
 * @brief Runs filter requests on a thread pool, one task per request.
 *
 * Every request takes a new generation number from an atomic counter. A task stops early once
 * a newer request exists or its future is cancelled, and the caller applies a result only if
 * `isCurrent(result.generation)`, so a slow old request can never overwrite a newer one. Tasks
 * hold their own `shared_ptr` to the index and to the counter, so they stay valid even if the
 * `LogFilter` is destroyed first.
 */
class LogFilter {
public:
    /**
     * @brief Creates a filter with generation 0.
     */
    LogFilter();

    /**
     * @brief Starts filtering on a pool task.
     *
     * The request takes the next generation number, which makes every earlier request out of
     * date.
     *
     * @param index The log index; the task keeps it alive. A null index gives no result.
     * @param spec The filter.
     * @param pool Thread pool to run on; null for the global pool.
     * @return A future yielding one result, or no result if the request was cancelled or
     *         became out of date before it finished.
     */
    [[nodiscard]] QFuture<LogFilterResult> request(LogIndexPtr index, LogFilterSpec spec,
                                                   QThreadPool* pool = nullptr);

    /**
     * @brief Makes every pending request out of date (for example when the run is closed).
     */
    void invalidate();

    /**
     * @brief Tells whether a result is from the newest request.
     *
     * @param generation The generation of a result.
     * @return True if no request or invalidate() came after it.
     */
    [[nodiscard]] bool isCurrent(quint64 generation) const;

    /**
     * @brief Returns the current generation number.
     *
     * @return The generation of the newest request or invalidation.
     */
    [[nodiscard]] quint64 generation() const;

private:
    std::shared_ptr<std::atomic<quint64>> generation_;  ///< Shared with the running tasks.
};

} // namespace abrviz
