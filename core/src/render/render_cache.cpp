#include "abrviz/core/render/render_cache.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace abrviz {

ClaimGuard::ClaimGuard(RenderCache* cache, QString key)
    : cache_(cache)
    , key_(std::move(key))
{
}

ClaimGuard::ClaimGuard(ClaimGuard&& other) noexcept
    : cache_(std::exchange(other.cache_, nullptr))
    , key_(std::move(other.key_))
{
}

ClaimGuard& ClaimGuard::operator=(ClaimGuard&& other) noexcept
{
    if (this != &other) {
        release();
        cache_ = std::exchange(other.cache_, nullptr);
        key_ = std::move(other.key_);
    }
    return *this;
}

ClaimGuard::~ClaimGuard()
{
    release();
}

void ClaimGuard::commit(const QString& path)
{
    if (auto* cache = std::exchange(cache_, nullptr))
        cache->finish(key_, &path);
}

void ClaimGuard::release() noexcept
{
    if (auto* cache = std::exchange(cache_, nullptr))
        cache->finish(key_, nullptr);
}

RenderCache::RenderCache(QString directory)
    : directory_(std::move(directory))
{
}

QString RenderCache::pathFor(const QString& key) const
{
    return QDir(directory_).filePath(key + QStringLiteral(".mp4"));
}

std::optional<QString> RenderCache::lookup(const QString& key)
{
    const std::scoped_lock lock(mutex_);
    if (const auto it = done_.constFind(key); it != done_.cend()) {
        if (QFileInfo::exists(*it))
            return *it;
        done_.erase(it);  // deleted behind our back
    }
    if (inProgress_.contains(key))
        return std::nullopt;
    // A file left by an earlier session. It was written through QSaveFile, so it is complete.
    const QString path = pathFor(key);
    if (QFileInfo(path).size() > 0) {
        done_.insert(key, path);
        return path;
    }
    return std::nullopt;
}

std::optional<ClaimGuard> RenderCache::tryClaim(const QString& key)
{
    const std::scoped_lock lock(mutex_);
    if (inProgress_.contains(key))
        return std::nullopt;
    inProgress_.insert(key);
    return ClaimGuard(this, key);
}

bool RenderCache::isClaimed(const QString& key) const
{
    const std::scoped_lock lock(mutex_);
    return inProgress_.contains(key);
}

void RenderCache::finish(const QString& key, const QString* path) noexcept
{
    const std::scoped_lock lock(mutex_);
    inProgress_.remove(key);
    if (path)
        done_.insert(key, *path);
}

} // namespace abrviz
