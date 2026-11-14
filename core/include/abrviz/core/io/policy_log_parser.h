#pragma once

/**
 * @file
 * @brief Parser for the decision lines of `policy.log`.
 */

#include <QByteArrayView>
#include <QString>

#include <optional>
#include <vector>

namespace abrviz {

/**
 * @brief One line of `policy.log`.
 *
 * Format:
 * `<t> ue-id: N seg(1-idx): k rung: r bitrate: b thr_est: e buf: s infer_ms: m reason: R`.
 */
struct PolicyDecision {
    double time = 0.0;  ///< simulation time in seconds (first word)
    int segmentOneBased = 0;  ///< line k belongs to `segments.jsonl` record k - 1
    int rung = 0;    ///< rung chosen (`rung:`); the guard's rung when it overrode the LLM
    QString reason;  ///< text after `reason:`
    bool guardOverride = false;   ///< reason contains `+guard(...)`
    std::optional<int> llmRung;   ///< `r<from>` in `guard(buf<B>:r<from>->r<to>)`
};

/**
 * @brief Reads the `policy.log` lines of one UE.
 *
 * Empty lines, lines starting with `#`, lines of other UEs and lines without a time, segment
 * or rung are skipped.
 *
 * @param text Contents of `policy.log`.
 * @param nodeId The UE's ns-3 node; lines with a different `ue-id` are skipped.
 * @return The decisions of that UE in file order.
 */
[[nodiscard]] std::vector<PolicyDecision> parsePolicyLog(QByteArrayView text, int nodeId);

} // namespace abrviz
