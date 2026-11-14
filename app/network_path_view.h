#pragma once

#include "abrviz/core/model/run_data.h"

#include <QWidget>

#include <memory>

class QTimer;

namespace abrviz {

class ReplayPlayer;
class Session;

/// The path every frame of the replay took, drawn across the top of the Replay view:
///
///     video server ── feeder link ──> LEO satellite ── satellite backhaul ──> IAB relay ── 5G access ──> viewer
///
/// with the numbers of the open run (feeder rate, satellite altitude, carrier, relay distance).
/// It follows the cursor: while a segment is downloading the links carry moving dashes and the
/// segment is named; during a stall the viewer is marked red. Without a run it shows the same
/// path without numbers (the welcome screen uses it that way).
class NetworkPathView final : public QWidget {
    Q_OBJECT

public:
    /// `session` and `player` may be null for the static diagram.
    NetworkPathView(Session* session, ReplayPlayer* player, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// What the strip says at the cursor (for tests): the activity line.
    [[nodiscard]] QString activityText() const { return activity_; }
    /// The label of each of the four nodes and three links, in path order.
    [[nodiscard]] QStringList labels() const;
    [[nodiscard]] bool isDownloading() const { return downloading_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void updateAnimation();

    Session* session_;      // not owned; may be null
    ReplayPlayer* player_;  // not owned; may be null
    QTimer* animation_;
    std::shared_ptr<const RunData> run_;
    QString activity_;
    bool downloading_ = false;
    bool stalled_ = false;
    bool running_ = false;
    double phase_ = 0.0;
};

} // namespace abrviz
