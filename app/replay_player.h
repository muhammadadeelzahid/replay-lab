#pragma once

/**
 * @file
 * @brief The replay player widget: the video of the open run with an information overlay.
 */

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
class QProgressBar;
class QPushButton;
class QVideoSink;

namespace abrviz {

class PlaybackActivity;
class RenderQueue;
class Session;

/**
 * @brief Shows the replay of the open run with an overlay and keeps it in step with the cursor.
 *
 * The overlay shows segment, rung, bitrate, resolution, buffer, state and engine. Player and
 * cursor are kept in step through the `PlaybackClock`: moving the cursor elsewhere seeks the
 * engine, and while the engine plays its position moves the cursor (Source::Player).
 *
 * The video is a `QGraphicsVideoItem` in a scene with the overlay drawn on top; both engines
 * write into its video sink. The engine is owned here through a `std::unique_ptr` and is
 * destroyed by `releaseEngine()` (the window calls it during shutdown, before the render queue
 * the engine uses is destroyed).
 */
class ReplayPlayer final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the player without an engine; call setEngine() to add one.
     *
     * @param session Session to follow; not owned, must outlive the player's engine.
     * @param queue Render queue the pre-rendered engine uses; not owned, must outlive the
     *        player's engine.
     * @param parent Parent widget, or null.
     */
    ReplayPlayer(Session* session, RenderQueue* queue, QWidget* parent = nullptr);
    /**
     * @brief Destroys the player and its engine (see releaseEngine()).
     */
    ~ReplayPlayer() override;

    /** @brief Not copyable or movable. */
    ReplayPlayer(const ReplayPlayer&) = delete;
    ReplayPlayer& operator=(const ReplayPlayer&) = delete;
    ReplayPlayer(ReplayPlayer&&) = delete;
    ReplayPlayer& operator=(ReplayPlayer&&) = delete;

    /**
     * @brief Replaces the engine; the old one is destroyed first.
     *
     * The new engine gets the video sink and the open run, and seeks to the cursor if it is
     * ready. If the GStreamer engine reports an error before it has shown a picture, it is
     * replaced by the pre-rendered engine (queued, outside the engine's own signal) and
     * `engineFellBack` is emitted. Emits `engineChanged` on success.
     *
     * @param engine Kind of engine to create.
     * @return False if `engine` is not available in this build; the player then has no engine.
     */
    bool setEngine(Engine engine);
    /**
     * @brief Destroys the engine, if any, and emits `engineChanged`.
     *
     * Destroying the engine cancels and waits for its render job and stops playback.
     *
     * @note The window calls this during shutdown, before the render queue is destroyed.
     */
    void releaseEngine();

    /**
     * @brief The current engine.
     *
     * @return The engine, owned by the player, or null if there is none.
     */
    [[nodiscard]] ReplaySource* engine() const { return engine_.get(); }
    /**
     * @brief Kind of the current engine.
     *
     * @return The kind, or no value if there is no engine.
     */
    [[nodiscard]] std::optional<Engine> engineKind() const { return engineKind_; }
    /**
     * @brief Text of the overlay, kept up to date even while the overlay is hidden.
     *
     * @return The overlay lines joined with newlines.
     */
    [[nodiscard]] QString overlayText() const;
    /**
     * @brief The sink both engines write into.
     *
     * @return The video item's sink, owned by the item.
     */
    [[nodiscard]] QVideoSink* videoSink() const;
    /**
     * @brief The message shown in the middle of the video (no run, no engine, error, render
     *        hint).
     *
     * @return The message, or an empty string if none is shown.
     */
    [[nodiscard]] QString messageText() const;
    /**
     * @brief Shows or hides the information drawn over the video.
     *
     * The overlay text is always kept up to date, whether it is visible or not.
     *
     * @param visible True to show the overlay.
     */
    void setOverlayVisible(bool visible);
    /**
     * @brief Whether the overlay is shown.
     *
     * @return True if the overlay is visible.
     */
    [[nodiscard]] bool isOverlayVisible() const { return overlayVisible_; }
    /**
     * @brief Whether the replay is running and moves the cursor.
     *
     * @return True if the engine is playing or stalled; false without an engine.
     */
    [[nodiscard]] bool isRunning() const;

public slots:
    /**
     * @brief Pauses a running replay, or starts one that is not running.
     *
     * @see play(), pause(), isRunning()
     */
    void togglePlay();
    /**
     * @brief Starts the replay.
     *
     * Does nothing without a ready engine. Restarts from the beginning if the replay has ended
     * or is within 50 ms of its end.
     */
    void play();
    /**
     * @brief Pauses the replay; does nothing without an engine.
     */
    void pause();
    /**
     * @brief Asks the engine to prepare the run (render the experienced video).
     *
     * Clears the last error. Does nothing without an engine.
     */
    void prepare();

signals:
    /**
     * @brief Emitted when the engine was created or released.
     */
    void engineChanged();
    /**
     * @brief Emitted after the engine changed state and the player has updated.
     *
     * @param state The engine's new state.
     */
    void stateChanged(abrviz::ReplaySource::State state);
    /**
     * @brief The GStreamer engine failed before showing a picture and was replaced by the
     *        pre-rendered one.
     *
     * @param reason The GStreamer engine's error message.
     */
    void engineFellBack(const QString& reason);

protected:
    /**
     * @brief Fits the video to the new size.
     *
     * @param event The resize event.
     */
    void resizeEvent(QResizeEvent* event) override;
    /**
     * @brief Fits the video when the player is shown.
     *
     * @param event The show event.
     */
    void showEvent(QShowEvent* event) override;

private:
    /**
     * @brief Hands the new run to the engine and clears the error and picture state.
     */
    void runChanged();
    /**
     * @brief Seeks the engine to a cursor moved by another panel and updates the overlay.
     *
     * @param time New cursor time in simulation seconds.
     * @param source Who moved the cursor; Source::Player (the player itself) does not seek.
     */
    void cursorMoved(double time, TimeCursor::Source source);
    /**
     * @brief Moves the cursor to the engine's position while the replay runs.
     *
     * Only a running engine moves the cursor; a seek reports the position it was asked for.
     *
     * @param replayTime Engine position in replay seconds.
     */
    void enginePositionChanged(double replayTime);
    /**
     * @brief Follows an engine state change.
     *
     * Records that the engine has shown a picture (Paused or Playing), holds a
     * `PlaybackActivity` while the replay runs, seeks to the cursor when the engine becomes
     * ready at position 0, updates the view and emits `stateChanged`.
     *
     * @param state The engine's new state.
     */
    void engineStateChanged(ReplaySource::State state);
    /**
     * @brief Rewrites the overlay for the cursor time: segment and rung, buffer level, stall or
     *        end, engine and both times.
     *
     * Hides the overlay without a run.
     */
    void updateOverlay();
    /**
     * @brief Sets the message centred on the video, or hides it when there is nothing to say.
     */
    void updateMessage();
    /**
     * @brief Shows the render bar while the pre-rendered engine has no video, with the
     *        progress bar in place of the button while it renders.
     */
    void updateControls();
    /**
     * @brief Scales the view so the whole 1920 x 1080 scene fits, keeping the aspect ratio.
     */
    void fitVideo();

    Session* session_;    ///< Not owned.
    RenderQueue* queue_;  ///< Not owned.
    QGraphicsScene* scene_;  ///< Holds the video, overlay and message items; child of the player.
    QGraphicsView* view_;  ///< Shows scene_; child of the player.
    QGraphicsVideoItem* video_;  ///< Video item the engine draws into; owned by scene_.
    QGraphicsRectItem* overlayBox_;  ///< Translucent box behind the overlay text.
    QGraphicsTextItem* overlay_;     ///< Overlay text; child of `overlayBox_`.
    QGraphicsTextItem* message_;     ///< Message centred on the video.
    QPushButton* renderButton_;  ///< Starts rendering for the pre-rendered engine.
    QProgressBar* progress_;  ///< Render progress, 0 to 1000; shown in place of renderButton_.
    QWidget* renderBar_;  ///< Render button and progress bar below the video.
    std::unique_ptr<ReplaySource> engine_;  ///< The replay engine; null if none is set.
    std::unique_ptr<PlaybackActivity> activity_;  ///< Held while the replay runs.
    std::optional<Engine> engineKind_;  ///< Kind of engine_; empty if none is set.
    QString error_;  ///< Last engine error; empty if none.
    bool engineShowedPicture_ = false;  ///< Reached Paused or Playing for the current run.
    bool overlayVisible_ = true;  ///< Whether the overlay is shown.
};

} // namespace abrviz
