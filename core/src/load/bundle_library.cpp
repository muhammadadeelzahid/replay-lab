#include "abrviz/core/load/bundle_library.h"

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/util/parallel_map.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

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
    summary.algorithm = run.value.policyName;
    summary.seed = run.value.rngRun;
    // The run number groups the algorithms' bundles of one video and rate: `..._run_3`.
    for (const QString& name : {run.value.runId, QDir(bundleDirectory).dirName()}) {
        const qsizetype at = name.lastIndexOf(QLatin1String("_run_"));
        bool ok = false;
        const int n = at >= 0 ? name.mid(at + 5).toInt(&ok) : -1;
        if (ok) {
            summary.runNumber = n;
            break;
        }
    }

    const VideoParts video = readVideo(bundleDirectory, run.value.videoName);
    summary.errors.append(video.info.errors);
    summary.videoTitle = video.info.value.title;
    summary.segments = video.info.value.segmentCount;

    const auto events = readPlayerEvents(bundleDirectory);
    summary.errors.append(events.errors);
    summary.stalls = static_cast<int>(events.value.stalls.size());
    summary.stallEpisodes = static_cast<int>(stallEpisodes(events.value.stalls).size());
    for (const Stall& stall : events.value.stalls)
        summary.stalledTime += stall.duration();

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
        if (a->linkRate.value_or(0) != b->linkRate.value_or(0))
            return a->linkRate.value_or(0) < b->linkRate.value_or(0);
        if (a->runNumber != b->runNumber)
            return a->runNumber < b->runNumber;
        // The algorithms of one experiment together, LLM first.
        const int ra = algorithmRank(a->algorithm);
        const int rb = algorithmRank(b->algorithm);
        return ra != rb ? ra < rb : a->algorithm < b->algorithm;
    });
}

int algorithmRank(const QString& key)
{
    static const QStringList order{QStringLiteral("llm"), QStringLiteral("robustmpc"),
                                   QStringLiteral("mpc"), QStringLiteral("bola")};
    const qsizetype at = order.indexOf(key.toLower());
    return at >= 0 ? static_cast<int>(at) : static_cast<int>(order.size());
}

QList<Experiment> groupExperiments(const QList<BundleSummaryPtr>& bundles)
{
    QList<Experiment> experiments;
    QHash<QString, qsizetype> byKey;
    for (const BundleSummaryPtr& bundle : bundles) {
        if (!bundle || !bundle->errors.isEmpty())
            continue;
        const QString key = QStringLiteral("%1/%2/%3")
                                .arg(bundle->videoName)
                                .arg(bundle->linkRate.value_or(0))
                                .arg(bundle->runNumber);
        auto it = byKey.find(key);
        if (it == byKey.end()) {
            Experiment e;
            e.key = key;
            e.videoName = bundle->videoName;
            e.videoTitle = bundle->videoTitle.isEmpty() ? bundle->videoName : bundle->videoTitle;
            e.linkRate = bundle->linkRate;
            e.runNumber = bundle->runNumber;
            it = byKey.insert(key, experiments.size());
            experiments.append(e);
        }
        experiments[*it].runs.append(bundle);
    }
    for (Experiment& e : experiments) {
        std::ranges::stable_sort(e.runs, [](const BundleSummaryPtr& a, const BundleSummaryPtr& b) {
            const int ra = algorithmRank(a->algorithm);
            const int rb = algorithmRank(b->algorithm);
            return ra != rb ? ra < rb : a->algorithm < b->algorithm;
        });
    }
    // By video in order of first appearance, then by rate.
    QStringList videos;
    for (const Experiment& e : std::as_const(experiments)) {
        if (!videos.contains(e.videoName))
            videos.append(e.videoName);
    }
    std::ranges::stable_sort(experiments, [&videos](const Experiment& a, const Experiment& b) {
        const qsizetype va = videos.indexOf(a.videoName);
        const qsizetype vb = videos.indexOf(b.videoName);
        if (va != vb)
            return va < vb;
        return a.linkRate.value_or(0) < b.linkRate.value_or(0);
    });
    return experiments;
}

} // namespace abrviz
