#pragma once

#include "abrviz/core/model/run_data.h"

#include <QString>

#include <optional>
#include <vector>

namespace abrviz {

/// One number that the exporter's `summary.json` also reports.
struct SummaryCheck {
    QString name;
    double computed = 0.0;
    std::optional<double> exported;

    /// Agrees with `summary.json` within 1 % (or 0.001 absolute for values near 0).
    [[nodiscard]] bool agrees() const;
};

/// QoE numbers for one session.
///
/// "Chosen" values describe the policy's decisions as recorded in `segments.jsonl` (one per
/// record); they are the definitions `summary.json` uses and are checked against it. "Played"
/// values describe what the simulated user saw, after the segment-shift correction: only the
/// segments that were shown, each at the rung it was played at.
struct QoeReport {
    int segmentsTotal = 0;
    int segmentsShown = 0;
    bool cutOff = false;
    int lastSegmentShown = -1;
    int lastSegmentFrames = 0;

    double averagePlayedBitrate = 0.0;
    int playedSwitches = 0;
    double averageChosenBitrate = 0.0;
    int chosenSwitches = 0;

    double startupDelay = 0.0;
    int stallCount = 0;
    double rebufferTime = 0.0;
    double averageThroughput = 0.0;

    int decisions = 0;  ///< decisions for segments that exist
    int guardOverrides = 0;
    double averageDecisionDelay = 0.0;
    double maxDecisionDelay = 0.0;

    std::vector<SummaryCheck> checks;

    [[nodiscard]] bool agreesWithSummary() const;
};

[[nodiscard]] QoeReport computeQoe(const RunData& run);

} // namespace abrviz
