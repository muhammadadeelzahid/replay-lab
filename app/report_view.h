#pragma once

#include "abrviz/core/metrics/qoe_metrics.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace abrviz {

class Session;

/// The QoE report of the open run: what the simulated user saw (the played segments, at their
/// played rungs), the policy's decisions, and the numbers `summary.json` also reports, each
/// compared with it.
class ReportView final : public QWidget {
    Q_OBJECT

public:
    explicit ReportView(Session* session, QWidget* parent = nullptr);

    /// The report shown (empty without a run).
    [[nodiscard]] const QoeReport& report() const { return report_; }
    /// The verdict line above the table.
    [[nodiscard]] QString verdict() const;
    /// Every row as "metric | value | summary.json | check", for tests.
    [[nodiscard]] QStringList rows() const;

private:
    void refresh();
    void addSection(const QString& title);
    void addRow(const QString& metric, const QString& value, const QString& exported = {},
                std::optional<bool> agrees = std::nullopt);

    Session* session_;  // not owned; outlives the view
    QLabel* verdict_;
    QTableWidget* table_;
    QoeReport report_;
};

} // namespace abrviz
