#include "abrviz/core/load/segment_shift.h"

#include <algorithm>

namespace abrviz {

ShiftResult applySegmentShift(const std::vector<SegmentRecord>& records, const VideoInfo& video,
                              const VideoSegmentTable& sizes)
{
    ShiftResult result;
    const auto count = static_cast<int>(
        std::min(records.size(), static_cast<std::size_t>(std::max(video.segmentCount, 0))));
    result.segments.reserve(static_cast<std::size_t>(count));
    result.decisions.reserve(records.size());

    for (int k = 0; k < count; ++k) {
        const SegmentRecord& download = records[static_cast<std::size_t>(k)];
        const int rung = k == 0 ? 0 : records[static_cast<std::size_t>(k - 1)].rung;
        Segment segment;
        segment.index = k;
        segment.requestTime = download.requestTime;
        segment.doneTime = download.doneTime;
        segment.playedRung = rung;
        segment.bytes = sizes.bytes(k, rung).value_or(0);
        segment.mediaPath = video.segmentPath(k, rung);
        segment.mediaDuration = video.nominalSegmentDuration;
        result.segments.push_back(std::move(segment));
    }

    for (std::size_t n = 0; n < records.size(); ++n) {
        const SegmentRecord& r = records[n];
        Decision d;
        d.record = static_cast<int>(n);
        d.forSegment = d.record + 1;
        d.time = r.doneTime;
        d.chosenRung = r.rung;
        d.bufferLevel = r.bufferLevel;
        d.throughputEstimate = r.throughputEstimate;
        d.rttMs = r.rttMs;
        d.lossRate = r.lossRate;
        d.decisionDelay = r.decisionDelay;
        d.guardOverride = r.fallback;  // the exporter's `fallback` flag marks guard overrides
        d.used = d.forSegment < video.segmentCount;
        result.decisions.push_back(d);
    }
    return result;
}

} // namespace abrviz
