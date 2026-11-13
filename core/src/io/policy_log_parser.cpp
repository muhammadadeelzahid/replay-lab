#include "abrviz/core/io/policy_log_parser.h"

#include "abrviz/core/io/log_tokens.h"
#include "abrviz/core/io/text_lines.h"

namespace abrviz {

namespace {

/// `llm_policy_model+guard(buf5.8:r5->r4)` -> 5
std::optional<int> guardFromRung(QByteArrayView reason)
{
    const qsizetype guard = reason.indexOf("guard(");
    if (guard < 0)
        return std::nullopt;
    const qsizetype from = reason.indexOf(":r", guard);
    const qsizetype arrow = reason.indexOf("->", from);
    if (from < 0 || arrow < 0)
        return std::nullopt;
    return toInt(reason.sliced(from + 2, arrow - from - 2));
}

} // namespace

std::vector<PolicyDecision> parsePolicyLog(QByteArrayView text, int nodeId)
{
    std::vector<PolicyDecision> result;
    forEachLine(text, [&](QByteArrayView line, qsizetype) {
        const QByteArrayView trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            return;
        const Tokens t(trimmed);
        if (t.intAfter("ue-id:") != nodeId)
            return;
        const auto time = toDouble(t.at(0));
        const auto segment = t.intAfter("seg(1-idx):");
        const auto rung = t.intAfter("rung:");
        if (!time || !segment || !rung)
            return;
        const QByteArrayView reason = t.after("reason:");
        PolicyDecision d;
        d.time = *time;
        d.segmentOneBased = *segment;
        d.rung = *rung;
        d.reason = QString::fromUtf8(reason);
        d.guardOverride = reason.contains("+guard(");
        d.llmRung = guardFromRung(reason);
        result.push_back(d);
    });
    return result;
}

} // namespace abrviz
