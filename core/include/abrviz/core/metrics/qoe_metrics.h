#pragma once

/**
 * @file
 * @brief Quality-of-experience numbers for a run, checked against the exporter's summary.
 */

#include "abrviz/core/model/run_data.h"

#include <QString>

#include <optional>
#include <vector>

namespace abrviz {

/**
 * @brief One number that the exporter's `summary.json` also reports.
 */
struct SummaryCheck {
    QString name;  ///< What is compared, for example "startup delay".
    double computed = 0.0;            ///< Value computed by the viewer.
    std::optional<double> exported;   ///< Value in `summary.json`; empty if missing.

    /**
     * @brief Tells whether the computed value agrees with `summary.json`.
     *
     * Agreement means within 1 % of the exported value, or within 0.001 absolute (for values
     * near 0).
     *
     * @return True if the values agree or there is no exported value.
     */
    [[nodiscard]] bool agrees() const;
};

/**
 * @brief QoE numbers for one session.
 *
 * "Chosen" values describe the policy's decisions as recorded in `segments.jsonl` (one per
 * record); they are the definitions `summary.json` uses and are checked against it. "Played"
 * values describe what the simulated user saw, after the segment-shift correction: only the
 * segments that were shown, each at the rung it was played at.
 */
struct QoeReport {
    int segmentsTotal = 0;  ///< segments in the video
    int segmentsShown = 0;  ///< playback entries with a known segment and rung
    bool cutOff = false;    ///< the simulation stopped before the last segment was fully played
    int lastSegmentShown = -1;   ///< -1 if nothing was shown
    int lastSegmentFrames = 0;  ///< frames of the last shown segment that were played; 0 if none

    double averagePlayedBitrate = 0.0;  ///< bits per second
    int playedSwitches = 0;  ///< bitrate changes between consecutive shown segments
    double averageChosenBitrate = 0.0;  ///< bits per second
    int chosenSwitches = 0;  ///< changes of chosen bitrate between consecutive records

    double startupDelay = 0.0;       ///< seconds from stream start to playback start
    int stallCount = 0;  ///< stalls in `player_events.jsonl`
    double rebufferTime = 0.0;       ///< seconds
    double averageThroughput = 0.0;  ///< bits per second, mean of the samples

    int decisions = 0;  ///< decisions for segments that exist
    int guardOverrides = 0;  ///< used decisions in which the buffer guard lowered the choice
    double averageDecisionDelay = 0.0;  ///< seconds
    double maxDecisionDelay = 0.0;      ///< seconds

    std::vector<SummaryCheck> checks;  ///< comparisons with `summary.json`

    /**
     * @brief Tells whether every check agrees with `summary.json`.
     *
     * @return True if all `checks` agree (also when there are none).
     * @see SummaryCheck::agrees()
     */
    [[nodiscard]] bool agreesWithSummary() const;
};

/**
 * @brief Computes the QoE numbers of a run and the checks against `summary.json`.
 *
 * Checks average chosen bitrate, chosen switches, average throughput, startup delay, stall
 * count and rebuffer time.
 *
 * @param run The loaded run.
 * @return The report.
 */
[[nodiscard]] QoeReport computeQoe(const RunData& run);

} // namespace abrviz
