#pragma once

#include "abrviz/core/model/run_data.h"

#include <QWidget>

#include <array>
#include <memory>

class QChart;
class QLabel;

namespace abrviz {

class ChartView;
class Session;

/// Five charts over simulation time, stacked with one shared x-axis: buffer (from the frame
/// lines, every 33 ms), throughput and the policy's estimate, bitrate on screen and chosen,
/// RTT and loss, and decision delay. A line marks the cursor in every chart, and a readout
/// above them shows the values at the cursor. Clicking or dragging a chart moves the cursor.
/// Missing values are gaps, never 0.
class ChartsPanel final : public QWidget {
    Q_OBJECT

public:
    enum class Chart { Buffer, Throughput, Bitrate, RttLoss, DecisionDelay };
    static constexpr int kChartCount = 5;

    explicit ChartsPanel(Session* session, QWidget* parent = nullptr);
    ~ChartsPanel() override;

    [[nodiscard]] QChart* chart(Chart chart) const;
    /// Number of series in `chart` with the given name (a line with gaps is several series).
    [[nodiscard]] int seriesCount(Chart chart, const QString& name) const;
    /// x position of the cursor line in `chart`'s coordinates, or NaN when it is hidden.
    [[nodiscard]] double cursorX(Chart chart) const;
    /// x position of `time` in `chart`'s coordinates.
    [[nodiscard]] double xAt(Chart chart, double time) const;
    [[nodiscard]] QString readout() const;

private:
    struct Plot;

    void rebuild();
    void updateCursor();
    void updateReadout();
    void fillBuffer(Plot& plot, const RunData& run);
    void fillThroughput(Plot& plot, const RunData& run);
    void fillBitrate(Plot& plot, const RunData& run);
    void fillRttLoss(Plot& plot, const RunData& run);
    void fillDecisionDelay(Plot& plot, const RunData& run);

    Session* session_;  // not owned; outlives the panel
    std::shared_ptr<const RunData> run_;
    std::array<std::unique_ptr<Plot>, kChartCount> plots_;
    QLabel* readout_;
};

} // namespace abrviz
