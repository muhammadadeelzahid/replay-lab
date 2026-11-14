#pragma once

/**
 * @file
 * @brief The QoE report view of the open run, checked against summary.json.
 */

#include "abrviz/core/metrics/qoe_metrics.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace abrviz {

class Session;

/**
 * @brief The QoE report of the open run.
 *
 * Shows what the simulated user saw (the played segments, at their played rungs), the policy's
 * decisions, and the numbers `summary.json` also reports, each compared with it. A verdict line
 * above the table sums up the comparison.
 */
class ReportView final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the view and fills it from the current run.
     *
     * @param session Session to read from; not owned, must outlive the view.
     * @param parent Parent widget, or null.
     */
    explicit ReportView(Session* session, QWidget* parent = nullptr);

    /**
     * @brief The report shown.
     *
     * @return The report; empty without a run.
     */
    [[nodiscard]] const QoeReport& report() const { return report_; }
    /**
     * @brief The verdict line above the table.
     *
     * @return The verdict text.
     */
    [[nodiscard]] QString verdict() const;
    /**
     * @brief Every row of the table as text, for tests.
     *
     * @return One string per row, "metric | value | summary.json | check"; missing cells are
     *         empty.
     */
    [[nodiscard]] QStringList rows() const;

private:
    /**
     * @brief Recomputes the report from the session's run and refills the table and verdict.
     */
    void refresh();
    /**
     * @brief Appends a bold section title spanning all columns.
     *
     * @param title The section title.
     */
    void addSection(const QString& title);
    /**
     * @brief Appends a metric row.
     *
     * @param metric Name of the metric (indented in the table).
     * @param value Value computed from the bundle.
     * @param exported Value from `summary.json`; empty if not compared.
     * @param agrees Whether the two values agree; no value leaves the check column empty.
     */
    void addRow(const QString& metric, const QString& value, const QString& exported = {},
                std::optional<bool> agrees = std::nullopt);

    Session* session_;  ///< Not owned; outlives the view.
    /** @brief Whether the computed values agree with `summary.json`, or that no run is open. */
    QLabel* verdict_;
    QTableWidget* table_;  ///< Metric, value, `summary.json` value and check columns.
    QoeReport report_;     ///< Report of the open run; empty if none.
};

} // namespace abrviz
