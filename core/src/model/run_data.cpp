#include "abrviz/core/model/run_data.h"

#include <QDir>

#include <algorithm>

namespace abrviz {

namespace {

/**
 * @brief Returns the items with a `time` in `[from, to]`.
 *
 * @tparam T Item type with a `time` member; `items` must be in time order.
 * @param items The items to search.
 * @param from Start time in seconds.
 * @param to End time in seconds.
 * @return A view into `items`; empty if none fall in the range.
 */
template <typename T>
std::span<const T> between(const std::vector<T>& items, double from, double to)
{
    if (!(from <= to))  // also a NaN bound
        return {};
    const auto first = std::ranges::lower_bound(items, from, {}, &T::time);
    const auto last = std::ranges::upper_bound(items, to, {}, &T::time);
    if (first >= last)
        return {};
    return {first, last};
}

} // namespace

QString VideoInfo::segmentPath(int segment, int rungIndex) const
{
    // The bundles use `r{rung}/seg_{segment:04d}.mp4`; other patterns are not supported.
    const QString file = QStringLiteral("r%1/seg_%2.mp4")
                             .arg(rungIndex)
                             .arg(segment, 4, 10, QLatin1Char('0'));
    return QDir(directory).filePath(file);
}

const Rung* VideoInfo::rung(int index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= ladder.size())
        return nullptr;
    return &ladder[static_cast<std::size_t>(index)];
}

const SegmentQuality* RunData::qualityAt(int segment, int rung) const
{
    const int rungs = static_cast<int>(video.ladder.size());
    if (segment < 0 || rung < 0 || rung >= rungs || rungs == 0)
        return nullptr;
    const auto index = static_cast<std::size_t>(segment) * static_cast<std::size_t>(rungs)
        + static_cast<std::size_t>(rung);
    return index < ladderQuality.size() ? &ladderQuality[index] : nullptr;
}

std::vector<StallEpisode> stallEpisodes(const std::vector<Stall>& stalls, double gap)
{
    std::vector<StallEpisode> episodes;
    for (const Stall& stall : stalls) {
        if (!episodes.empty() && stall.start - episodes.back().end < gap) {
            StallEpisode& last = episodes.back();
            last.end = std::max(last.end, stall.end);
            last.stalledTime += stall.duration();
            ++last.pauses;
            last.terminal = last.terminal || stall.terminal;
            continue;
        }
        episodes.push_back({stall.start, stall.end, stall.duration(), 1, stall.terminal});
    }
    return episodes;
}

QString algorithmDisplayName(const QString& key)
{
    const QString k = key.toLower();
    if (k == QLatin1String("llm"))
        return QStringLiteral("LLM");
    if (k == QLatin1String("robustmpc"))
        return QStringLiteral("RobustMPC");
    if (k == QLatin1String("mpc"))
        return QStringLiteral("MPC");
    if (k == QLatin1String("bola"))
        return QStringLiteral("BOLA");
    return key.isEmpty() ? QStringLiteral("ABR") : key;
}

const SegmentPlayback* RunData::playbackAt(double simTime) const
{
    const auto it = std::ranges::upper_bound(playback, simTime, {},
                                             &SegmentPlayback::firstFrameTime);
    if (it == playback.begin())
        return nullptr;
    return &*std::prev(it);
}

std::span<const Sample> RunData::samplesBetween(double from, double to) const
{
    return between(samples, from, to);
}

std::span<const BufferPoint> RunData::bufferBetween(double from, double to) const
{
    return between(buffer, from, to);
}

bool RunData::playbackCutOff() const
{
    if (playback.empty())
        return false;
    const auto& last = playback.back();
    return last.segment + 1 < video.segmentCount || last.framesPlayed < kFramesPerSegment;
}

bool RunData::hasWarning(Warning::Kind kind) const
{
    return std::ranges::any_of(warnings, [kind](const Warning& w) { return w.kind == kind; });
}

} // namespace abrviz
