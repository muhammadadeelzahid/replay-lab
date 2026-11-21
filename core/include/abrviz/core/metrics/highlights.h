#pragma once

/**
 * @file
 * @brief Finds the experiments where one algorithm's picture is clearly better than the
 *        others', and the moment that shows it best.
 */

#include "abrviz/core/model/run_data.h"

#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace abrviz {

/**
 * @brief An experiment where the chosen algorithm looks better than every other one.
 */
struct Highlight {
    QString directory;          ///< the chosen algorithm's run
    QString rivalDirectory;     ///< the run to show it against
    QString rivalAlgorithm;     ///< its `policy.name`
    double vmaf = 0.0;          ///< mean VMAF the viewer saw in the chosen run
    double bestOtherVmaf = 0.0; ///< the highest mean VMAF of the other runs
    double meanOtherVmaf = 0.0; ///< the other runs' mean VMAF, averaged
    double stalledTime = 0.0;   ///< seconds the chosen run stalled
    double leastOtherStalledTime = 0.0;  ///< the least any other run stalled
    int segment = -1;           ///< first segment of the moment shown
    double momentVmaf = 0.0;    ///< the chosen run's VMAF over the moment
    double rivalMomentVmaf = 0.0;  ///< the rival's VMAF over the same segments
    double time = 0.0;          ///< simulation time of the moment in the chosen run
    double score = 0.0;         ///< how clear the advantage is; higher first
};

/// Smallest VMAF lead over the best other run that counts as clearly better (about the
/// smallest difference viewers notice).
inline constexpr double kHighlightMinGain = 2.0;

/// Segments in the moment shown: about 6 s of video.
inline constexpr int kHighlightSegments = 3;

/**
 * @brief Tells whether an algorithm's run in an experiment looks clearly better, and where.
 *
 * It qualifies when its mean VMAF (of what was on screen) is at least kHighlightMinGain above
 * every other run's, and it stalled no more than half a second longer than the run that stalled
 * least. The score is its VMAF gain over the best other run plus half its gain over the others'
 * average. The moment is the run of kHighlightSegments segments, shown by both, where its VMAF
 * most exceeds one of the other runs'; that run is the rival.
 *
 * @param runs The experiment's runs (same video, rate and seed), any order.
 * @param algorithm The algorithm to look at (`policy.name`, any case).
 * @return The highlight, or nothing if the run is missing, has no VMAF, or does not qualify.
 */
[[nodiscard]] std::optional<Highlight> findHighlight(const std::vector<std::shared_ptr<const RunData>>& runs,
                                                     const QString& algorithm);

} // namespace abrviz
