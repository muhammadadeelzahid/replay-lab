#pragma once

/**
 * @file
 * @brief The pre-rendered replay engine, which plays the run's `experienced.mp4`.
 */

#include "abrviz/core/render/render_queue.h"
#include "abrviz/core/util/future_guard.h"
#include "abrviz/media/replay_source.h"

#include <QFutureWatcher>
#include <QPointer>

#include <optional>

class QMediaPlayer;

namespace abrviz {

/**
 * @brief Plays the run's `experienced.mp4` through `QMediaPlayer`.
 *
 * The file is made by the `RenderQueue`; if it is in the cache it is used at once, otherwise
 * `prepare()` renders it. Stalls are frozen frames in the file and cut-off playback ends where it
 * stopped, so the file's time is the replay time.
 */
class PrerenderedSource final : public ReplaySource {
    Q_OBJECT

public:
    /**
     * @brief Creates the engine with its media player and render watcher.
     *
     * @param queue The render queue that makes and caches the file (not owned); must outlive
     *              the source.
     * @param parent Qt parent.
     */
    explicit PrerenderedSource(RenderQueue* queue, QObject* parent = nullptr);

    /**
     * @brief Cancels a running render, waits for it and stops the player.
     *
     * The watcher and player are disconnected first, so no render result or player signal
     * reaches the object while it is destroyed.
     */
    ~PrerenderedSource() override;

    /**
     * @brief Neither copyable nor movable: a QObject that owns a player and a running render.
     */
    PrerenderedSource(const PrerenderedSource&) = delete;
    PrerenderedSource& operator=(const PrerenderedSource&) = delete;
    PrerenderedSource(PrerenderedSource&&) = delete;
    PrerenderedSource& operator=(PrerenderedSource&&) = delete;

    /**
     * @brief The engine's name as shown to the user.
     *
     * @return "Pre-rendered" (translated).
     */
    [[nodiscard]] QString name() const override;

    /**
     * @brief Replaces the run.
     *
     * Cancels and waits for a render of the previous run, stops the player and clears the file,
     * a pending seek and a pending play. The state becomes `Idle` and the position 0. If the
     * run's file is already in the cache it is loaded at once (state `Preparing`, then `Paused`);
     * otherwise `prepare()` must be called to render it.
     *
     * @param run The run to show; null leaves the engine empty.
     */
    void setRun(std::shared_ptr<const RunData> run) override;

    /**
     * @brief Sets the sink the player shows its frames in.
     *
     * @param sink The video sink (not owned); may be null.
     */
    void setVideoSink(QVideoSink* sink) override;

    /**
     * @brief Starts playback, from the start if playback has ended.
     *
     * Ends a running scrub first. Before the file is loaded, the request is kept and playback
     * starts once the media is loaded.
     */
    void play() override;

    /**
     * @brief Pauses playback and drops a pending play request.
     */
    void pause() override;

    /**
     * @brief Moves to a replay time (the file's time).
     *
     * Before the file is loaded, the target is kept and applied once the media is loaded. A seek
     * after the end moves the state back to `Paused`. While paused, the frame at the target is
     * shown with a short scrub (see `showFrameAt()`).
     *
     * @param replayTime Target position (seconds); rounded to milliseconds.
     */
    void seek(double replayTime) override;

    /**
     * @brief Whether the file is known (cached or rendered).
     *
     * @return True once the file to play is set; it may still be loading (`Preparing`).
     */
    [[nodiscard]] bool isReady() const override { return !file_.isEmpty(); }

    /**
     * @brief Queues a render of the run's file.
     *
     * Does nothing without a run, when the file is ready or while a render runs. Sets the state
     * to `Preparing` and reports progress through `preparationProgress`, starting at 0.
     */
    void prepare() override;

    /**
     * @brief The file being played.
     *
     * @return The path of `experienced.mp4`; empty until it is ready.
     */
    [[nodiscard]] const QString& file() const { return file_; }

private:
    /**
     * @brief Handles the end of a render.
     *
     * A cancelled render, or one without a result, returns the state to `Idle`. A failed render
     * sets `Error` and emits `errorOccurred`. Otherwise reports progress 1 and loads the file.
     */
    void renderFinished();

    /**
     * @brief Sets the file and hands it to the player; the state is `Preparing` until it loads.
     *
     * @param file Path of the rendered video.
     */
    void load(const QString& file);

    /**
     * @brief Follows the player's media status.
     *
     * Loaded media ends `Preparing`: the state becomes `Paused` and a pending seek and play are
     * applied. End of media sets `Ended`; invalid media sets `Error` and emits `errorOccurred`.
     */
    void mediaStatusChanged();

    /**
     * @brief Mirrors the player's playback state as `Playing`, `Paused` or `Ended`.
     *
     * Ignored while scrubbing, preparing or in `Error`.
     */
    void playbackStateChanged();

    /**
     * @brief Shows the frame at a position while paused.
     *
     * AVFoundation shows no picture for a seek while paused, so the player plays until the sink
     * receives a frame (or 500 ms pass) and `finishScrub()` pauses it again at the target. If a
     * scrub is already running it only takes the new target. Does nothing without a sink.
     *
     * @param ms Target position in the file (milliseconds).
     */
    void showFrameAt(qint64 ms);

    /**
     * @brief Ends a running scrub: pauses the player and moves it back to the scrub target.
     *
     * Does nothing if no scrub is running.
     */
    void finishScrub();

    RenderQueue* queue_;  ///< not owned
    std::shared_ptr<const RunData> run_;  ///< the run to play; null if none
    QMediaPlayer* player_;  ///< child
    QPointer<QVideoSink> sink_;  ///< where frames are shown; not owned, may be null
    QFutureWatcher<RenderResult>* watcher_;  ///< child
    FutureGuard<RenderResult> render_;       ///< the running render; cancelled and awaited on reset
    QString file_;                           ///< the file to play; empty until ready
    std::optional<double> pendingSeek_;      ///< seek requested before the file was loaded
    bool playWhenReady_ = false;             ///< play requested before the file was loaded
    /**
     * AVFoundation shows no picture for a seek while paused, so the engine plays until the
     * first frame arrives and pauses again; the state stays Paused meanwhile.
     */
    bool scrubbing_ = false;
    qint64 scrubTarget_ = 0;                  ///< position to return to after the scrub (ms)
    QMetaObject::Connection scrubConnection_; ///< sink frame signal that ends the scrub
};

} // namespace abrviz
