#pragma once

#include "abrviz/core/logs/log_index.h"

#include <QFuture>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class QThreadPool;

namespace abrviz {

/// What the log console shows.
///
/// `text` holds words separated by spaces; a line must contain every word. `"two words"`
/// matches a phrase, and `-word` excludes lines containing it. Matching ignores case unless the
/// word has an upper-case letter.
struct LogFilterSpec {
    static constexpr int kAllUes = -2;

    QString text;
    /// A node id, or `kAllUes`. Choosing a UE keeps the lines that belong to no UE (setup,
    /// progress), which give the context of that UE's lines.
    int ue = kAllUes;
    LogLevel minLevel = LogLevel::Info;

    friend bool operator==(const LogFilterSpec&, const LogFilterSpec&) = default;
};

/// Rows of the index that match, in index order.
using LogRows = std::vector<qint32>;

/// Filters `index` synchronously. `stop` is polled every few thousand lines; when it returns
/// true the function returns early with an incomplete result.
[[nodiscard]] LogRows filterLog(const LogIndex& index, const LogFilterSpec& spec,
                                const std::function<bool()>& stop = {});

struct LogFilterResult {
    quint64 generation = 0;
    LogRows rows;
};

/// Runs filter requests on a thread pool, one task per request.
///
/// Every request takes a new generation number from an atomic counter. A task stops early once
/// a newer request exists or its future is cancelled, and the caller applies a result only if
/// `isCurrent(result.generation)`, so a slow old request can never overwrite a newer one. Tasks
/// hold their own `shared_ptr` to the index and to the counter, so they stay valid even if the
/// `LogFilter` is destroyed first.
class LogFilter {
public:
    LogFilter();

    [[nodiscard]] QFuture<LogFilterResult> request(LogIndexPtr index, LogFilterSpec spec,
                                                   QThreadPool* pool = nullptr);

    /// Makes every pending request out of date (for example when the run is closed).
    void invalidate();

    [[nodiscard]] bool isCurrent(quint64 generation) const;
    [[nodiscard]] quint64 generation() const;

private:
    std::shared_ptr<std::atomic<quint64>> generation_;
};

} // namespace abrviz
