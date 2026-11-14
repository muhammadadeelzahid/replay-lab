#pragma once

#include <QWidget>

class QGridLayout;
class QLabel;

namespace abrviz {

class ReplayPlayer;
class Session;

/// Beside the replay: what the simulated viewer saw at the cursor (segment, rung, resolution,
/// bitrate, buffer, stall), the policy's last decision before it, the network as last
/// measured, and the engine and times. Fixed width, so its changing
/// values never relayout the window.
class MomentPanel final : public QWidget {
    Q_OBJECT

public:
    MomentPanel(Session* session, ReplayPlayer* player, QWidget* parent = nullptr);

    /// The value shown for `field` (for tests), e.g. "Segment" or "Buffer".
    [[nodiscard]] QString value(const QString& field) const;

private:
    void refresh();
    void addHeading(const QString& title);
    QLabel* addRow(const QString& name);

    Session* session_;      // not owned
    ReplayPlayer* player_;  // not owned
    QLabel* segment_;
    QLabel* quality_;
    QLabel* bitrate_;
    QLabel* buffer_;
    QLabel* status_;
    QLabel* decisionTime_;
    QLabel* chosen_;
    QLabel* feeder_;
    QLabel* satellite_;
    QLabel* carrier_;
    QLabel* relay_;
    QLabel* estimate_;
    QLabel* rtt_;
    QLabel* engine_;
    QLabel* simTime_;
    QLabel* replayTime_;
    QList<QPair<QString, QLabel*>> fields_;
    QGridLayout* grid_ = nullptr;
};

} // namespace abrviz
