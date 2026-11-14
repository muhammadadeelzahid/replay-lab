#include "abrviz/core/metrics/qoe_metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace abrviz {

namespace {

constexpr double kRelativeTolerance = 0.01;
constexpr double kAbsoluteTolerance = 0.001;

template <typename T>
int countChanges(const std::vector<T>& values)
{
    int changes = 0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] != values[i - 1])
            ++changes;
    }
    return changes;
}

double mean(const std::vector<double>& values)
{
    if (values.empty())
        return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0)
         / static_cast<double>(values.size());
}

std::optional<double> toDouble(std::optional<int> value)
{
    return value ? std::optional<double>(*value) : std::nullopt;
}

} // namespace

bool SummaryCheck::agrees() const
{
    if (!exported)
        return true;
    const double difference = std::abs(computed - *exported);
    return difference <= kAbsoluteTolerance
        || difference <= kRelativeTolerance * std::abs(*exported);
}

bool QoeReport::agreesWithSummary() const
{
    return std::ranges::all_of(checks, &SummaryCheck::agrees);
}

QoeReport computeQoe(const RunData& run)
{
    QoeReport r;
    r.segmentsTotal = run.video.segmentCount;
    r.cutOff = run.playbackCutOff();
    if (!run.playback.empty()) {
        r.lastSegmentShown = run.playback.back().segment;
        r.lastSegmentFrames = run.playback.back().framesPlayed;
    }

    // What was played: every segment the player showed, at its played rung.
    std::vector<qint64> played;
    for (const SegmentPlayback& p : run.playback) {
        if (p.segment < 0 || static_cast<std::size_t>(p.segment) >= run.segments.size())
            continue;
        const Rung* rung =
            run.video.rung(run.segments[static_cast<std::size_t>(p.segment)].playedRung);
        if (rung)
            played.push_back(rung->bitrate);
    }
    r.segmentsShown = static_cast<int>(played.size());
    if (!played.empty())
        r.averagePlayedBitrate = static_cast<double>(std::accumulate(played.begin(),
                                                                     played.end(), qint64{0}))
                               / static_cast<double>(played.size());
    r.playedSwitches = countChanges(played);

    // What the policy chose, one value per record, as summary.json defines it.
    std::vector<qint64> chosen;
    for (const Decision& d : run.decisions) {
        if (const Rung* rung = run.video.rung(d.chosenRung))
            chosen.push_back(rung->bitrate);
    }
    if (!chosen.empty())
        r.averageChosenBitrate = static_cast<double>(std::accumulate(chosen.begin(),
                                                                     chosen.end(), qint64{0}))
                               / static_cast<double>(chosen.size());
    r.chosenSwitches = countChanges(chosen);

    r.startupDelay = run.playbackStart - run.streamStart;
    r.stallCount = static_cast<int>(run.stalls.size());
    for (const Stall& stall : run.stalls)
        r.rebufferTime += stall.duration();

    std::vector<double> throughput;
    for (const Sample& s : run.samples) {
        if (s.throughput)
            throughput.push_back(*s.throughput);
    }
    r.averageThroughput = mean(throughput);

    std::vector<double> delays;
    for (const Decision& d : run.decisions) {
        if (!d.used)
            continue;
        ++r.decisions;
        if (d.guardOverride)
            ++r.guardOverrides;
        if (d.decisionDelay)
            delays.push_back(*d.decisionDelay);
    }
    r.averageDecisionDelay = mean(delays);
    if (!delays.empty())
        r.maxDecisionDelay = *std::ranges::max_element(delays);

    const ExporterSummary& s = run.summary;
    r.checks = {
        {QStringLiteral("average bitrate (chosen)"), r.averageChosenBitrate, s.averageBitrate},
        {QStringLiteral("quality switches (chosen)"), static_cast<double>(r.chosenSwitches),
         toDouble(s.switchCount)},
        {QStringLiteral("average throughput"), r.averageThroughput, s.averageThroughput},
        {QStringLiteral("startup delay"), r.startupDelay, s.startupDelay},
        {QStringLiteral("stalls"), static_cast<double>(r.stallCount), toDouble(s.stallCount)},
        {QStringLiteral("rebuffer time"), r.rebufferTime, s.rebufferTime},
    };
    return r;
}

} // namespace abrviz
