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
    int stalls = 0;    ///< number of stalls in `player_events.jsonl`, as the player logged them
    int stallEpisodes = 0;  ///< rebuffering episodes (see `stallEpisodes()`)
    double stalledTime = 0.0;  ///< seconds stalled in total
    QString algorithm;  ///< `policy.name` of `run.json`, e.g. `llm` or `robustmpc`
    int seed = 0;       ///< `network.rng_run` of `run.json`
    /** @brief `N` of a `_run_N` suffix of the run id or folder; -1 without one. */
    int runNumber = -1;
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
 * @brief Runs of one video at one feeder rate and run number, streamed by different ABR
 *        algorithms: what the viewer compares side by side.
 */
struct Experiment {
    QString key;         ///< video, rate and run number, e.g. `chef_cooking/18000000/3`
    QString videoName;   ///< the video's folder name
    QString videoTitle;  ///< the video's title
    std::optional<qint64> linkRate;  ///< feeder link rate in bits per second
    int runNumber = -1;  ///< the shared `run_N`; -1 without one
    /** @brief The runs: LLM, RobustMPC, MPC, BOLA first, then others by name. */
    QList<BundleSummaryPtr> runs;
};

/**
 * @brief Groups bundles into experiments by video, feeder rate and run number.
 *
 * Bundles with errors are left out. A bundle with no other algorithm of its video, rate and run
 * number forms an experiment of one run.
 *
 * @param bundles The bundles of a library.
 * @return The experiments, by video in the order the videos first appear, then by rate.
 */
[[nodiscard]] QList<Experiment> groupExperiments(const QList<BundleSummaryPtr>& bundles);

/**
 * @brief Orders ABR algorithm keys for display: `llm`, `robustmpc`, `mpc`, `bola`, then others
 *        alphabetically.
 *
 * @param key An algorithm key.
 * @return A sort rank; lower comes first.
 */
[[nodiscard]] int algorithmRank(const QString& key);

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
 * @brief Sorts bundles by video name, link rate and run number, then by algorithm (LLM,
 *        RobustMPC, MPC, BOLA, others), so that the runs of an experiment are together.
 *
 * A bundle without a link rate sorts as rate 0.
 *
 * @param bundles The list to sort in place.
 */
void sortLibrary(QList<BundleSummaryPtr>& bundles);

} // namespace abrviz
