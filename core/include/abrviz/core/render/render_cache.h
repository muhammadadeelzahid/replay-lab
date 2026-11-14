#pragma once

/**
 * @file
 * @brief The on-disk cache of rendered videos and the claims that keep one job per key.
 */

#include <QHash>
#include <QSet>
#include <QString>

#include <mutex>
#include <optional>

namespace abrviz {

class RenderCache;

/**
 * @brief A claim on one cache key, returned by `RenderCache::tryClaim`.
 *
 * While it exists no other job renders the same key. `commit` records the finished file; if
 * the guard is destroyed without a commit (the job failed or was cancelled) the claim is
 * released and nothing is recorded. Move-only.
 */
class ClaimGuard {
public:
    /**
     * @brief Takes over the claim of `other`, which no longer holds it.
     *
     * @param other The guard to move from.
     */
    ClaimGuard(ClaimGuard&& other) noexcept;

    /**
     * @brief Releases the current claim and takes over the claim of `other`.
     *
     * Self-assignment does nothing.
     *
     * @param other The guard to move from.
     * @return This guard.
     */
    ClaimGuard& operator=(ClaimGuard&& other) noexcept;

    /** @brief Not copyable: a claim has a single owner. */
    ClaimGuard(const ClaimGuard&) = delete;
    ClaimGuard& operator=(const ClaimGuard&) = delete;

    /**
     * @brief Releases the claim if it was neither committed nor moved, recording nothing.
     */
    ~ClaimGuard();

    /**
     * @brief Records the finished file for the key and releases the claim.
     *
     * Does nothing if the claim was already committed or moved.
     *
     * @param path The rendered file.
     */
    void commit(const QString& path);

    /**
     * @brief Returns the claimed key.
     *
     * @return The key.
     */
    [[nodiscard]] const QString& key() const { return key_; }

private:
    friend class RenderCache;

    /**
     * @brief Creates a guard for a key the cache has already marked as in progress.
     *
     * @param cache The cache; not owned.
     * @param key The claimed key.
     */
    ClaimGuard(RenderCache* cache, QString key);

    /**
     * @brief Releases the claim without recording a file. Safe to call more than once.
     */
    void release() noexcept;

    /** @brief The cache; not owned; null once released, committed or moved from. */
    RenderCache* cache_ = nullptr;
    QString key_;  ///< the claimed cache key
};

/**
 * @brief Rendered files on disk, by key, and the keys being rendered now.
 *
 * Thread-safe.
 */
class RenderCache {
public:
    /**
     * @brief Creates a cache that stores its files in `directory`.
     *
     * The directory is not created here.
     *
     * @param directory The cache folder.
     */
    explicit RenderCache(QString directory);

    /** @brief Not copyable: claims hold a pointer to the cache. */
    RenderCache(const RenderCache&) = delete;
    RenderCache& operator=(const RenderCache&) = delete;

    /**
     * @brief Returns the cache folder.
     *
     * @return The folder given to the constructor.
     */
    [[nodiscard]] const QString& directory() const { return directory_; }

    /**
     * @brief Returns where the file for `key` is stored.
     *
     * @param key The cache key.
     * @return `<directory>/<key>.mp4`.
     */
    [[nodiscard]] QString pathFor(const QString& key) const;

    /**
     * @brief Returns the finished file for `key`, if it exists.
     *
     * A recorded file that was deleted is forgotten. A non-empty file left by an earlier
     * session is accepted (it was written through `QSaveFile`, so it is complete) unless the
     * key is being rendered now.
     *
     * @param key The cache key.
     * @return The path, or nothing if there is no finished file.
     */
    [[nodiscard]] std::optional<QString> lookup(const QString& key);

    /**
     * @brief Claims `key` for rendering.
     *
     * @param key The cache key.
     * @return The claim, or nothing if another job holds it.
     */
    [[nodiscard]] std::optional<ClaimGuard> tryClaim(const QString& key);

    /**
     * @brief Tells whether a job holds the claim on `key`.
     *
     * @param key The cache key.
     * @return True while the key is being rendered.
     */
    [[nodiscard]] bool isClaimed(const QString& key) const;

private:
    friend class ClaimGuard;

    /**
     * @brief Ends the claim on `key`, recording `path` as its file if given.
     *
     * @param key The claimed key.
     * @param path The rendered file, or null to record nothing.
     */
    void finish(const QString& key, const QString* path) noexcept;

    const QString directory_;  ///< folder holding the rendered files
    mutable std::mutex mutex_;          ///< Guards `done_` and `inProgress_`.
    QHash<QString, QString> done_;      ///< Finished files by key.
    QSet<QString> inProgress_;          ///< Claimed keys.
};

} // namespace abrviz
