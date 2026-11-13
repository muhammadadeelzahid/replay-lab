#pragma once

#include "abrviz/core/io/bundle_files.h"
#include "abrviz/core/model/run_data.h"

#include <vector>

namespace abrviz {

struct ShiftResult {
    std::vector<Segment> segments;
    std::vector<Decision> decisions;
};

/// Turns the exporter's segment records into played segments and decisions.
///
/// Record `n` holds the timing of download `n` but the rung chosen for segment `n + 1` (see
/// docs/simulation-data.md). So segment 0 is played at rung 0, segment `k` at the rung of
/// record `k - 1`, bytes are looked up again with the played rung, and the last record becomes
/// a decision for a segment past the end of the video (`used == false`).
[[nodiscard]] ShiftResult applySegmentShift(const std::vector<SegmentRecord>& records,
                                            const VideoInfo& video,
                                            const VideoSegmentTable& sizes);

} // namespace abrviz
