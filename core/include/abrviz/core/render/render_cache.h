#pragma once

#include <QHash>
#include <QSet>
#include <QString>

#include <mutex>
#include <optional>

namespace abrviz {

class RenderCache;

/// A claim on one cache key, returned by `RenderCache::tryClaim`. While it exists no other job
/// renders the same key. `commit` records the finished file; if the guard is destroyed without
/// a commit (the job failed or was cancelled) the claim is released and nothing is recorded.
/// Move-only.
class ClaimGuard {
public:
    ClaimGuard(ClaimGuard&& other) noexcept;
    ClaimGuard& operator=(ClaimGuard&& other) noexcept;
    ClaimGuard(const ClaimGuard&) = delete;
    ClaimGuard& operator=(const ClaimGuard&) = delete;
    ~ClaimGuard();

    void commit(const QString& path);
    [[nodiscard]] const QString& key() const { return key_; }

private:
    friend class RenderCache;
    ClaimGuard(RenderCache* cache, QString key);
    void release() noexcept;

    RenderCache* cache_ = nullptr;  // not owned; null once released or moved from
    QString key_;
};

/// Rendered files on disk, by key, and the keys being rendered now. Thread-safe.
class RenderCache {
public:
    explicit RenderCache(QString directory);

    RenderCache(const RenderCache&) = delete;
    RenderCache& operator=(const RenderCache&) = delete;

    [[nodiscard]] const QString& directory() const { return directory_; }
    /// Where the file for `key` is stored.
    [[nodiscard]] QString pathFor(const QString& key) const;

    /// The finished file for `key`, if it exists.
    [[nodiscard]] std::optional<QString> lookup(const QString& key);
    /// Claims `key` for rendering; empty if another job holds it.
    [[nodiscard]] std::optional<ClaimGuard> tryClaim(const QString& key);
    [[nodiscard]] bool isClaimed(const QString& key) const;

private:
    friend class ClaimGuard;
    void finish(const QString& key, const QString* path) noexcept;

    const QString directory_;
    mutable std::mutex mutex_;
    QHash<QString, QString> done_;
    QSet<QString> inProgress_;
};

} // namespace abrviz
