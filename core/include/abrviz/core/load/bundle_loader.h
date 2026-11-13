#pragma once

#include "abrviz/core/load/load_result.h"

#include <QFuture>

class QThreadPool;

namespace abrviz {

/// Loads a bundle on `pool` without blocking the caller.
///
/// The small files the rest depends on (`run.json`, the video description and the segment
/// records) are read first; then the other files, the chunks of `sim.log` and the media
/// durations are read in parallel and joined with `QtFuture::whenAll`. No pool thread waits for
/// another pool thread.
///
/// The future reports progress (one step per file, log chunk and media file), can be cancelled,
/// and yields one `LoadResult`. A cancelled load yields no result.
[[nodiscard]] QFuture<LoadResult> loadBundle(const QString& directory, QThreadPool* pool = nullptr);

} // namespace abrviz
