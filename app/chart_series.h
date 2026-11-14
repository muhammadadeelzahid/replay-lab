#pragma once

/**
 * @file
 * @brief Helpers that turn run data into chart point lists, with gaps for missing values.
 */

#include <QList>
#include <QPointF>

#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <vector>

namespace abrviz {

/**
 * @brief Splits items into runs of consecutive points that have a value.
 *
 * A missing value ends the current run, so the chart shows a gap instead of drawing the line
 * through 0.
 *
 * @tparam Range Input range of items.
 * @tparam XFn Callable (or member pointer) giving an item's x value as something convertible
 *         to `double`.
 * @tparam YFn Callable (or member pointer) giving an item's y value as `std::optional<double>`.
 * @param items The items, in x order.
 * @param x Gives the x value of an item.
 * @param y Gives the y value of an item, or no value if it is missing.
 * @param scale Factor applied to every y value (for example 1e-6 for bit/s to Mbit/s).
 * @return One point list per run; empty if no item has a value.
 */
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

/**
 * @brief One step of a step line: a value held from `start` to `end`.
 *
 * @see stepRuns()
 */
struct StepInterval {
    double start = 0.0;  ///< Start of the interval (x units, usually seconds).
    double end = 0.0;    ///< End of the interval.
    double value = 0.0;  ///< Value held over the interval.
};

/**
 * @brief Points of a step line that holds each value from `start` to `end`.
 *
 * The line has a gap wherever one interval does not begin where the previous one ended (more
 * than `tolerance` apart).
 *
 * @param steps The intervals, in order.
 * @param tolerance Largest distance between the end of one interval and the start of the next
 *        that is still drawn as connected.
 * @return One point list per connected run, two points per interval.
 */

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
