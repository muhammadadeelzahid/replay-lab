#pragma once

#include <QList>
#include <QPointF>

#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <vector>

namespace abrviz {

/// Splits `items` into runs of consecutive points that have a value. A missing value ends the
/// current run, so the chart shows a gap instead of drawing the line through 0.
template <std::ranges::input_range Range, typename XFn, typename YFn>
    requires std::convertible_to<std::invoke_result_t<XFn&, std::ranges::range_reference_t<Range>>,
                                 double>
             && std::same_as<std::remove_cvref_t<std::invoke_result_t<
                                 YFn&, std::ranges::range_reference_t<Range>>>,
                             std::optional<double>>
[[nodiscard]] std::vector<QList<QPointF>> contiguousRuns(Range&& items, XFn x, YFn y,
                                                         double scale = 1.0)
{
    std::vector<QList<QPointF>> runs;
    bool open = false;
    for (auto&& item : items) {
        const std::optional<double> value = std::invoke(y, item);
        if (!value) {
            open = false;
            continue;
        }
        if (!open) {
            runs.emplace_back();
            open = true;
        }
        runs.back().append(QPointF(std::invoke(x, item), *value * scale));
    }
    return runs;
}

/// Points of a step line that holds each value from `start` to `end`, with a gap wherever
/// one interval does not begin where the previous one ended (more than `tolerance` apart).
struct StepInterval {
    double start = 0.0;
    double end = 0.0;
    double value = 0.0;
};

[[nodiscard]] inline std::vector<QList<QPointF>> stepRuns(const std::vector<StepInterval>& steps,
                                                          double tolerance = 0.05)
{
    std::vector<QList<QPointF>> runs;
    double lastEnd = 0.0;
    for (const StepInterval& step : steps) {
        if (runs.empty() || step.start - lastEnd > tolerance)
            runs.emplace_back();
        runs.back().append(QPointF(step.start, step.value));
        runs.back().append(QPointF(step.end, step.value));
        lastEnd = step.end;
    }
    return runs;
}

} // namespace abrviz
