#pragma once

/**
 * @file
 * @brief Asynchronous, parallel loading of a bundle.
 */

#include "abrviz/core/load/load_result.h"

#include <QFuture>

class QThreadPool;

namespace abrviz {

/**
 * @brief Loads a bundle on `pool` without blocking the caller.
 *
 * The small files the rest depends on (`run.json`, the video description and the segment
 * records) are read first; then the other files, the chunks of `sim.log` and the media
 * durations are read in parallel, one task each. The task that finishes last merges the log
 * chunks and assembles the result. No pool thread waits for another pool thread.
 *
 * The future reports progress (one step per file, log chunk and media file), can be cancelled,
 * and yields one `LoadResult`. A cancelled load yields no result.
 *
 * @param directory Directory of the bundle.
 * @param pool Thread pool to run on; null for the global pool.
 * @return The future of the load.
 * @see loadBundleSync()
 */
[[nodiscard]] QFuture<LoadResult> loadBundle(const QString& directory, QThreadPool* pool = nullptr);

} // namespace abrviz
