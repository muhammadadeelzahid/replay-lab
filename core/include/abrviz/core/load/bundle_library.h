#pragma once

#include <QFuture>
#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include <optional>

class QThreadPool;

namespace abrviz {

/// What the library shows about a bundle without loading it: read from `run.json`,
/// `summary.json`, `player_events.jsonl` and the video description only.
struct BundleSummary {
    QString directory;
    QString runId;
    QString videoName;
    QString videoTitle;
    std::optional<qint64> linkRate;
    int segments = 0;
    int stalls = 0;
    /// Estimated from the play time in `player_events.jsonl`: true if the simulation stopped
    /// more than one segment before the end. A cut-off in the last segment is not detected
    /// here; the full load finds the exact segment and frame.
    bool cutOff = false;
    double logDecisionAgreement = 1.0;
    QStringList errors;  ///< empty when the bundle looks readable
};

using BundleSummaryPtr = QSharedPointer<const BundleSummary>;

/// Folders directly inside `directory` (or `directory` itself) that contain `run.json`.
[[nodiscard]] QStringList findBundles(const QString& directory);

/// Reads the summary of one bundle.
[[nodiscard]] BundleSummary readBundleSummary(const QString& bundleDirectory);

/// Finds the bundles in `directory` and reads their summaries in parallel, one task per bundle.
/// The list is in folder-name order.
[[nodiscard]] QFuture<QList<BundleSummaryPtr>> scanLibrary(const QString& directory,
                                                           QThreadPool* pool = nullptr);

/// Sorts by video, then by link rate.
void sortLibrary(QList<BundleSummaryPtr>& bundles);

} // namespace abrviz
