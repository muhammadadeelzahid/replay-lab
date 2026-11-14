#pragma once

#include "abrviz/media/replay_source.h"
#include "abrviz/media/replay_source_factory.h"
#include "time_cursor.h"

#include <QWidget>

#include <memory>
#include <optional>

class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsTextItem;
class QGraphicsVideoItem;
class QGraphicsView;
class QLabel;
class QProgressBar;
class QPushButton;

namespace abrviz {

class RenderQueue;
class Session;

/// Shows the replay of the open run with an overlay (segment, rung, bitrate, resolution, buffer,
/// state, engine) and keeps it in step with the cursor through the `PlaybackClock`: moving the
/// cursor elsewhere seeks the engine, and while the engine plays its position moves the cursor.
///
/// The video is a `QGraphicsVideoItem` in a scene with the overlay drawn on top; both engines
/// write into its video sink. The engine is owned here through a `std::unique_ptr` and is
/// destroyed by `releaseEngine()` (the window calls it during shutdown, before the render queue
/// the engine uses is destroyed).
class ReplayPlayer final : public QWidget {
    Q_OBJECT

public:
    /// `session` and `queue` must outlive the player's engine.
    ReplayPlayer(Session* session, RenderQueue* queue, QWidget* parent = nullptr);
    ~ReplayPlayer() override;

    ReplayPlayer(const ReplayPlayer&) = delete;
    ReplayPlayer& operator=(const ReplayPlayer&) = delete;
    ReplayPlayer(ReplayPlayer&&) = delete;
    ReplayPlayer& operator=(ReplayPlayer&&) = delete;

    /// Replaces the engine (the old one is destroyed first). Returns false if `engine` is not
    /// available in this build.
    bool setEngine(Engine engine);
    void releaseEngine();

    [[nodiscard]] ReplaySource* engine() const { return engine_.get(); }
    [[nodiscard]] std::optional<Engine> engineKind() const { return engineKind_; }
    [[nodiscard]] QString overlayText() const;
    [[nodiscard]] QString messageText() const;

public slots:
    void togglePlay();
    void play();
    void pause();
    /// Asks the engine to prepare the run (render the experienced video).
    void prepare();

signals:
    void engineChanged();
    void stateChanged(abrviz::ReplaySource::State state);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void runChanged();
    void cursorMoved(double time, TimeCursor::Source source);
    void enginePositionChanged(double replayTime);
    void engineStateChanged(ReplaySource::State state);
    void updateOverlay();
    void updateMessage();
    void updateControls();
    void fitVideo();

    Session* session_;    // not owned
    RenderQueue* queue_;  // not owned
    QGraphicsScene* scene_;
    QGraphicsView* view_;
    QGraphicsVideoItem* video_;
    QGraphicsRectItem* overlayBox_;
    QGraphicsTextItem* overlay_;
    QGraphicsTextItem* message_;
    QPushButton* playButton_;
    QPushButton* renderButton_;
    QProgressBar* progress_;
    QLabel* timeLabel_;
    QLabel* engineLabel_;
    std::unique_ptr<ReplaySource> engine_;
    std::optional<Engine> engineKind_;
    QString error_;
};

} // namespace abrviz
