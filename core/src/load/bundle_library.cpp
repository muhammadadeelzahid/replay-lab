#include "abrviz/core/load/bundle_library.h"

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/util/parallel_map.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace abrviz {

QStringList findBundles(const QString& directory)
{
    const QDir dir(directory);
    if (QFileInfo::exists(dir.filePath(QStringLiteral("run.json"))))
        return {dir.absolutePath()};
    QStringList bundles;
    const auto children = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& child : children) {
        if (QFileInfo::exists(dir.filePath(child + QStringLiteral("/run.json"))))
            bundles.append(QDir(dir.filePath(child)).absolutePath());
    }
    return bundles;
}

BundleSummary readBundleSummary(const QString& bundleDirectory)
{
    BundleSummary summary;
    summary.directory = bundleDirectory;
    const auto run = readRunInfo(bundleDirectory);
    summary.errors.append(run.errors);
    if (!run.ok())
        return summary;
    summary.runId = run.value.runId;
    summary.videoName = run.value.videoName;
    summary.linkRate = run.value.feederLinkRate;
    summary.logDecisionAgreement = run.value.logDecisionAgreement;

    const VideoParts video = readVideo(bundleDirectory, run.value.videoName);
    summary.errors.append(video.info.errors);
    summary.videoTitle = video.info.value.title;
    summary.segments = video.info.value.segmentCount;

    const auto events = readPlayerEvents(bundleDirectory);
    summary.errors.append(events.errors);
    summary.stalls = static_cast<int>(events.value.stalls.size());

    // Time spent playing, divided by the play time of one segment, is about the number of
    // segments shown. A segment the player skipped takes no play time, so one missing segment
    // is tolerated; the cut-off runs are short by several segments.
    const auto& ev = events.value;
    if (ev.playbackStart && ev.playbackEnd && run.value.playerSegmentDuration > 0.0) {
        double stalled = 0.0;
        for (const Stall& stall : ev.stalls)
            stalled += stall.duration();
        const double played = *ev.playbackEnd - *ev.playbackStart - stalled;
        const double shown = played / run.value.playerSegmentDuration;
        summary.cutOff = shown < summary.segments - 1.5;
    }
    return summary;
}

QFuture<QList<BundleSummaryPtr>> scanLibrary(const QString& directory, QThreadPool* pool)
{
    return parallelMap(
        findBundles(directory),
        [](const QString& bundle) { return BundleSummaryPtr::create(readBundleSummary(bundle)); },
        pool);
}

void sortLibrary(QList<BundleSummaryPtr>& bundles)
{
    std::ranges::sort(bundles, [](const BundleSummaryPtr& a, const BundleSummaryPtr& b) {
        if (a->videoName != b->videoName)
            return a->videoName < b->videoName;
        return a->linkRate.value_or(0) < b->linkRate.value_or(0);
    });
}

} // namespace abrviz
