#pragma once

#include <QByteArrayView>
#include <QString>

#include <optional>
#include <vector>

namespace abrviz {

/// One line of `policy.log`:
/// `<t> ue-id: N seg(1-idx): k rung: r bitrate: b thr_est: e buf: s infer_ms: m reason: R`.
struct PolicyDecision {
    double time = 0.0;
    int segmentOneBased = 0;  ///< line k belongs to `segments.jsonl` record k - 1
    int rung = 0;
    QString reason;
    bool guardOverride = false;   ///< reason contains `+guard(...)`
    std::optional<int> llmRung;   ///< `r<from>` in `guard(buf<B>:r<from>->r<to>)`
};

/// Reads the `policy.log` lines of one UE (`ue-id` equal to `nodeId`).
[[nodiscard]] std::vector<PolicyDecision> parsePolicyLog(QByteArrayView text, int nodeId);

} // namespace abrviz
