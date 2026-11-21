#include "abrviz/core/metrics/highlights.h"

#include "abrviz/core/metrics/qoe_metrics.h"

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>

namespace abrviz {

namespace {

/// Each shown segment's VMAF and the time its first frame appeared.
struct Shown {
    double vmaf;
    double firstFrame;
};

std::map<int, Shown> shownSegments(const RunData& run)
{
    std::map<int, Shown> shown;
    for (const SegmentPlayback& p : run.playback) {
        const auto it = std::ranges::find(run.segments, p.segment, &Segment::index);
        if (it != run.segments.end() && it->quality.vmaf)
            shown.emplace(p.segment, Shown{*it->quality.vmaf, p.firstFrameTime});
    }
    return shown;
}

} // namespace

std::optional<Highlight> findHighlight(const std::vector<std::shared_ptr<const RunData>>& runs,
                                       const QString& algorithm)
{
    std::shared_ptr<const RunData> chosen;
    std::vector<std::shared_ptr<const RunData>> others;
    for (const auto& run : runs) {
        if (!run)
            continue;
        if (run->info.policyName.compare(algorithm, Qt::CaseInsensitive) == 0)
            chosen = run;
        else
            others.push_back(run);
    }
    if (!chosen || others.empty())
        return std::nullopt;

    const QoeReport mine = computeQoe(*chosen);
    if (!mine.meanVmafShown)
        return std::nullopt;
    Highlight h;
    h.directory = chosen->info.bundleDirectory;
    h.vmaf = *mine.meanVmafShown;
    h.stalledTime = mine.rebufferTime;
    h.leastOtherStalledTime = std::numeric_limits<double>::infinity();
    h.bestOtherVmaf = -1.0;
    double sum = 0.0;
    for (const auto& other : others) {
        const QoeReport r = computeQoe(*other);
        if (!r.meanVmafShown)
            return std::nullopt;
        h.bestOtherVmaf = std::max(h.bestOtherVmaf, *r.meanVmafShown);
        h.leastOtherStalledTime = std::min(h.leastOtherStalledTime, r.rebufferTime);
        sum += *r.meanVmafShown;
    }
    h.meanOtherVmaf = sum / static_cast<double>(others.size());
    if (h.vmaf < h.bestOtherVmaf + kHighlightMinGain || h.stalledTime > h.leastOtherStalledTime + 0.5)
        return std::nullopt;
    h.score = (h.vmaf - h.bestOtherVmaf) + 0.5 * (h.vmaf - h.meanOtherVmaf);

    // The moment: the window of segments, shown by both runs, with the largest VMAF gap.
    const std::map<int, Shown> mineShown = shownSegments(*chosen);
    double bestGap = -1e9;
    for (const auto& other : others) {
        const std::map<int, Shown> theirs = shownSegments(*other);
        for (const auto& [first, start] : mineShown) {
            double a = 0.0;
            double b = 0.0;
            bool complete = true;
            for (int s = first; s < first + kHighlightSegments && complete; ++s) {
                const auto m = mineShown.find(s);
                const auto t = theirs.find(s);
                complete = m != mineShown.end() && t != theirs.end();
                if (complete) {
                    a += m->second.vmaf;
                    b += t->second.vmaf;
                }
            }
            if (!complete)
                continue;
            a /= kHighlightSegments;
            b /= kHighlightSegments;
            if (a - b > bestGap) {
                bestGap = a - b;
                h.segment = first;
                h.momentVmaf = a;
                h.rivalMomentVmaf = b;
                h.time = start.firstFrame;
                h.rivalDirectory = other->info.bundleDirectory;
                h.rivalAlgorithm = other->info.policyName;
            }
        }
    }
    if (h.segment < 0)
        return std::nullopt;
    return h;
}

} // namespace abrviz
