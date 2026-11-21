#include "abrviz/core/playback/comparison_clock.h"

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

/// An empty clock for out-of-range indices.
const PlaybackClock kEmptyClock;

/// A null run for out-of-range indices.
const std::shared_ptr<const RunData> kNoRun;

} // namespace

ComparisonClock::ComparisonClock(std::vector<std::shared_ptr<const RunData>> runs,
                                 Alignment alignment)
    : alignment_(alignment)
{
    entries_.reserve(runs.size());
    for (auto& run : runs) {
        Entry entry;
        if (run)
            entry.clock = PlaybackClock(*run);
        entry.run = std::move(run);
        if (entry.run) {
            const double length = alignment_ == Alignment::SessionTime
                                       ? entry.clock.simEnd() - entry.run->streamStart
                                       : entry.clock.mediaDuration();
            if (std::isfinite(length))
                duration_ = std::max(duration_, length);
        }
        entries_.push_back(std::move(entry));
    }
}

const std::shared_ptr<const RunData>& ComparisonClock::run(int index) const
{
    return index >= 0 && index < runCount() ? entries_[static_cast<std::size_t>(index)].run : kNoRun;
}

const PlaybackClock& ComparisonClock::clock(int index) const
{
    return index >= 0 && index < runCount() ? entries_[static_cast<std::size_t>(index)].clock
                                            : kEmptyClock;
}

ComparisonClock::RunPosition ComparisonClock::positionOf(int index, double master) const
{
    RunPosition p;
    if (index < 0 || index >= runCount() || !entries_[static_cast<std::size_t>(index)].run)
        return p;
    const Entry& e = entries_[static_cast<std::size_t>(index)];
    // NaN counts as the start; infinities clamp to the ends like any other time.
    const double t = std::isnan(master) ? 0.0 : std::clamp(master, 0.0, duration_);
    if (alignment_ == Alignment::SessionTime) {
        p.sim = e.run->streamStart + t;
        p.replay = e.clock.simToReplay(p.sim);
        p.position = e.clock.positionAtSim(p.sim);
    } else {
        p.replay = e.clock.mediaToReplay(t);
        p.sim = e.clock.replayToSim(p.replay);
        p.position = e.clock.positionAtReplay(p.replay);
    }
    p.starting = p.position.beforeStart;
    p.ended = p.position.afterEnd;
    return p;
}

double ComparisonClock::masterOf(int index, double replay) const
{
    if (index < 0 || index >= runCount() || !entries_[static_cast<std::size_t>(index)].run)
        return 0.0;
    const Entry& e = entries_[static_cast<std::size_t>(index)];
    const double master = alignment_ == Alignment::SessionTime
                              ? e.clock.replayToSim(replay) - e.run->streamStart
                              : e.clock.replayToMedia(replay);
    return std::isfinite(master) ? std::clamp(master, 0.0, duration_) : 0.0;
}

} // namespace abrviz
