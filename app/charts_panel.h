#pragma once

/**
 * @file
 * @brief The charts panel: five time-series charts of the open run with a shared cursor.
 */

#include "abrviz/core/model/run_data.h"

#include <QWidget>

#include <array>
#include <memory>

class QChart;
class QLabel;

namespace abrviz {

class ChartView;
class Session;

/**
 * @brief Five charts over simulation time, stacked with one shared x-axis.
 *
 * The charts are: buffer (from the frame lines, every 33 ms), throughput and the policy's
 * estimate, bitrate on screen and chosen, RTT and loss, and decision delay. A line marks the
 * cursor in every chart, and a readout above them shows the values at the cursor. Clicking or
 * dragging a chart moves the cursor (Source::Charts). Missing values are gaps, never 0.
 */
class ChartsPanel final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief The charts, top to bottom; the value is the chart's index.
     */
    enum class Chart { Buffer, Throughput, Bitrate, RttLoss, DecisionDelay };
    static constexpr int kChartCount = 5;  ///< Number of values in Chart.

    /**
     * @brief Creates the charts and fills them from the current run.
     *
     * @param session Session to read from; not owned, must outlive the panel.
     * @param parent Parent widget, or null.
     */
    explicit ChartsPanel(Session* session, QWidget* parent = nullptr);
    /**
     * @brief Destroys the panel and its charts.
     */
    ~ChartsPanel() override;

    /**
     * @brief The Qt chart behind one of the charts.
     *
     * @param chart Which chart.
     * @return The chart, owned by its view; never null.
     */
    [[nodiscard]] QChart* chart(Chart chart) const;
    /**
     * @brief Number of series in a chart with the given name.
     *
     * A line with gaps is several series.
     *
     * @param chart Which chart.
     * @param name Series name, such as "buffer" or "estimate".
     * @return The number of matching series.
     */
    [[nodiscard]] int seriesCount(Chart chart, const QString& name) const;
    /**
     * @brief x position of the cursor line.
     *
     * @param chart Which chart.
     * @return The x coordinate in `chart`'s coordinates, or NaN when the line is hidden (no
     *         run open).
     */
    [[nodiscard]] double cursorX(Chart chart) const;
    /**
     * @brief x position of a time.
     *
     * @param chart Which chart.
     * @param time Simulation time in seconds.
     * @return The x coordinate in `chart`'s coordinates; the left edge of the plot area if the
     *         x-axis range is empty.
     */
    [[nodiscard]] double xAt(Chart chart, double time) const;
    /**
     * @brief Text of the readout above the charts.
     *
     * @return The readout as rich text.
     */
    [[nodiscard]] QString readout() const;

private:
    struct Plot;

    /**
     * @brief Takes the session's run and refills every chart.
     *
     * Sets every x-axis to [0, max(1, Session::endTime())], fills the series, fits the y-axes
     * and updates the cursor line and the readout.
     */
    void rebuild();
    /**
     * @brief Moves the cursor line in every chart to the cursor time; hides it without a run.
     */
    void updateCursor();
    /**
     * @brief Writes the values at the cursor time into the readout.
     *
     * Shows the time, buffer level, stall or segment on screen with its rung, and the policy's
     * last throughput estimate, RTT and loss; "-" where a value is missing.
     */
    void updateReadout();
    /**
     * @brief Fills the buffer chart: the buffer level and the stalls as shaded bands.
     *
     * @param plot The buffer chart.
     * @param run The open run.
     */
    void fillBuffer(Plot& plot, const RunData& run);
    /**
     * @brief Fills the throughput chart: the rate of each download and the policy's estimate.
     *
     * The y-axis stops at 1.1 times the feeder link rate (or the highest estimate without a
     * link limit); faster values run off the top and the legend says so.
     *
     * @param plot The throughput chart.
     * @param run The open run.
     */
    void fillThroughput(Plot& plot, const RunData& run);
    /**
     * @brief Fills the bitrate chart: the bitrate on screen, the chosen rungs and the guard
     *        overrides with the LLM's choice.
     *
     * The line on screen stops for stalls. The y-axis always fits the whole ladder, so rung
     * changes are comparable between runs.
     *
     * @param plot The bitrate chart.
     * @param run The open run.
     */
    void fillBitrate(Plot& plot, const RunData& run);
    /**
     * @brief Fills the RTT and loss chart: RTT on the left axis, loss on the right one.
     *
     * @param plot The RTT and loss chart.
     * @param run The open run.
     */
    void fillRttLoss(Plot& plot, const RunData& run);
    /**
     * @brief Fills the decision-delay chart with the time the policy took for each used
     *        decision.
     *
     * @param plot The decision-delay chart.
     * @param run The open run.
     */
    void fillDecisionDelay(Plot& plot, const RunData& run);

    Session* session_;  ///< Not owned; outlives the panel.
    std::shared_ptr<const RunData> run_;  ///< The run shown; null if none.
    std::array<std::unique_ptr<Plot>, kChartCount> plots_;  ///< Indexed by Chart.
    QLabel* readout_;  ///< Values at the cursor as rich text, above the charts; child of the panel.
};

} // namespace abrviz
