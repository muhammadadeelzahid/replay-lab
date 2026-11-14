#pragma once

/**
 * @file
 * @brief The segment-shift correction that turns exporter records into segments and decisions.
 */

#include "abrviz/core/io/bundle_files.h"
#include "abrviz/core/model/run_data.h"

#include <vector>

namespace abrviz {

/**
 * @brief Played segments and decisions produced by applySegmentShift().
 */
struct ShiftResult {
    /** @brief One per record up to the video's segment count, at the played rung. */
    std::vector<Segment> segments;
    std::vector<Decision> decisions;  ///< One per record, in record order.
};

/**
 * @brief Turns the exporter's segment records into played segments and decisions.
 *
 * Record `n` holds the timing of download `n` but the rung chosen for segment `n + 1` (see
 * docs/simulation-data.md). So segment 0 is played at rung 0, segment `k` at the rung of
 * record `k - 1`, bytes are looked up again with the played rung, and the last record becomes
 * a decision for a segment past the end of the video (`used == false`).
 *
 * Segments get the nominal media duration; the loader replaces it with the real one. The
 * exporter's `fallback` flag becomes `Decision::guardOverride`.
 *
 * @param records The records of `segments.jsonl`, in order.
 * @param video The video description; limits the number of segments.
 * @param sizes Size of every (segment, rung) pair.
 * @return One segment per record up to the video's segment count, and one decision per record.
 */
[[nodiscard]] ShiftResult applySegmentShift(const std::vector<SegmentRecord>& records,
                                            const VideoInfo& video,
                                            const VideoSegmentTable& sizes);

} // namespace abrviz
