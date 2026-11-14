#pragma once

/**
 * @file
 * @brief Finding the bundles in a folder and reading their summaries without loading them.
 */

#include <QFuture>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include <optional>

class QThreadPool;

namespace abrviz {

/**
 * @brief What the library shows about a bundle without loading it.
 *
 * Read from `run.json`, `summary.json`, `player_events.jsonl` and the video description only.
 */
struct BundleSummary {
    QString directory;   ///< absolute path of the bundle
    QString runId;       ///< `run_id` of `run.json`
    QString videoName;   ///< `ues[0].video` of `run.json`
    QString videoTitle;  ///< `title` of `video.json`
    std::optional<qint64> linkRate;  ///< feeder link rate in bits per second; empty if uncongested
    int segments = 0;  ///< `segment_count` of `video.json`
    int stalls = 0;    ///< number of stalls in `player_events.jsonl`
    /// Estimated from the play time in `player_events.jsonl`: true if the simulation stopped
    /// more than one segment before the end. A cut-off in the last segment is not detected
    /// here; the full load finds the exact segment and frame.
    bool cutOff = false;
    /** @brief `sim_log_decision_agreement` of `run.json`; 1 if missing. */
    double logDecisionAgreement = 1.0;
    QStringList errors;  ///< empty when the bundle looks readable
};

/** @brief Shared, immutable bundle summary. */
using BundleSummaryPtr = QSharedPointer<const BundleSummary>;

/**
 * @brief Finds the bundles in a folder.
 *
 * @param directory The folder to search.
 * @return `directory` itself if it contains `run.json`; otherwise the folders directly inside
 *         it that contain `run.json`, as absolute paths in name order.
 */
[[nodiscard]] QStringList findBundles(const QString& directory);

/**
 * @brief Reads the summary of one bundle.
 *
 * Stops after `run.json` if that file has errors.
 *
 * @param bundleDirectory Directory of the bundle.
 * @return The summary; its `errors` lists every problem found.
 */
[[nodiscard]] BundleSummary readBundleSummary(const QString& bundleDirectory);

/**
 * @brief Finds the bundles in a folder and reads their summaries in parallel.
 *
 * One task per bundle. The list is in folder-name order.
 *
 * @param directory The folder to search.
 * @param pool Thread pool to run on; null for the global pool.
 * @return A future yielding the summaries.
 * @see findBundles(), readBundleSummary()
 */
[[nodiscard]] QFuture<QList<BundleSummaryPtr>> scanLibrary(const QString& directory,
                                                           QThreadPool* pool = nullptr);

/**
 * @brief Sorts bundles by video name, then by link rate.
 *
 * A bundle without a link rate sorts as rate 0.
 *
 * @param bundles The list to sort in place.
 */
void sortLibrary(QList<BundleSummaryPtr>& bundles);

} // namespace abrviz
