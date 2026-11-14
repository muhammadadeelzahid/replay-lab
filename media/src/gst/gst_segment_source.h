#pragma once

/**
 * @file
 * @brief The GStreamer replay engine, which plays the run's segment files directly.
 */

#include "gst_ptr.h"
#include "pipeline_guard.h"

#include "abrviz/core/playback/playback_clock.h"
#include "abrviz/media/replay_source.h"

#include <QElapsedTimer>
#include <QPointer>

#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

class QTimer;
class QVideoSink;

namespace abrviz {

/**
 * @brief Plays the run's segment files directly with GStreamer.
 *
 * The pipeline is:
 *
 *     splitmuxsrc ! decodebin3 ! videoconvert ! videoscale
 *                 ! video/x-raw,format=NV12,width=1920,height=1080 ! appsink sync=false
 *
 * `splitmuxsrc` reads the played segments, in order and at their played rungs, as one stream
 * whose time is the media time of the `PlaybackClock`.
 *
 * Timing is done here, not by the sink. The appsink does not sync to a clock; it keeps a few
 * decoded frames queued (the streaming threads block when it is full). While playing, a timer
 * on the GUI thread advances a replay clock, maps it to media time through the spans of the
 * replay timeline (runs of segments, and stalls), and shows the newest queued frame that is due.
 * A stall holds the last frame while the replay clock runs on; playback ends at the replay
 * duration, which is where a cut-off run stopped. Seeks are flushing and accurate; the first
 * frame after a seek is shown at once, also while paused.
 *
 * Threads: GStreamer's streaming threads only decode into the appsink queue, which this
 * object reads on the GUI thread. A `std::jthread` pops bus messages and posts plain values to
 * the GUI thread. The destructor stops the bus thread and then the pipeline.
 */
class GstSegmentSource final : public ReplaySource {
    Q_OBJECT
    Q_PROPERTY(qint64 framesDecoded READ framesDecoded)
    Q_PROPERTY(qint64 framesShown READ framesShown)

public:
    /**
     * @brief Creates an idle engine with its presentation and preroll timers.
     *
     * @param parent Qt parent.
     */
    explicit GstSegmentSource(QObject* parent = nullptr);

    /**
     * @brief Stops the bus thread, then the pipeline.
     *
     * The bus thread posts to this object, so it is joined first; then the pipeline is set to
     * NULL, which joins the streaming threads. Calls the bus thread has already queued are
     * dropped with the object.
     */
    ~GstSegmentSource() override;

    /**
     * @brief Neither copyable nor movable: threads hold `this`.
     */
    GstSegmentSource(const GstSegmentSource&) = delete;
    GstSegmentSource& operator=(const GstSegmentSource&) = delete;
    GstSegmentSource(GstSegmentSource&&) = delete;
    GstSegmentSource& operator=(GstSegmentSource&&) = delete;

    /**
     * @brief The engine's name as shown to the user.
     *
     * @return "GStreamer" (translated).
     */
    [[nodiscard]] QString name() const override;

    /**
     * @brief Replaces the run and builds a new pipeline for it.
     *
     * Stops the old pipeline and timers, resets the clock, spans and counters, and sets the
     * state to `Idle` and the position to 0. With a run that has spans, builds and starts the
     * pipeline and sets `Preparing` until the first preroll; a build failure sets `Error` and
     * emits `errorOccurred`. If no picture arrives within the preroll timeout (8 s) the pipeline
     * is stopped and the engine fails the same way, so the player can fall back.
     *
     * @param run The run to show; null leaves the engine empty.
     */
    void setRun(std::shared_ptr<const RunData> run) override;

    /**
     * @brief Sets the sink frames are shown in.
     *
     * @param sink The video sink (not owned); may be null, then frames are counted but not shown.
     */
    void setVideoSink(QVideoSink* sink) override;

    /**
     * @brief Starts the replay clock.
     *
     * Before the preroll the request is kept and applied when it completes. Playing at or past
     * the end starts again from 0. The state becomes `Playing`, or `Stalled` inside a stall.
     * Does nothing without a pipeline or when already running.
     */
    void play() override;

    /**
     * @brief Stops the replay clock at the current replay time and sets `Paused`.
     *
     * Also drops a play request made before the preroll.
     */
    void pause() override;

    /**
     * @brief Seeks the pipeline to the media time of a replay time.
     *
     * Before the preroll the target is kept and applied when it completes. The target is clamped
     * to the replay duration; a seek after the end moves back to `Paused`. Seeking into a stall
     * shows the last frame before it. The seek is flushing and accurate, and the first frame
     * after it is shown at once, also while paused. Does nothing without a pipeline.
     *
     * @param replayTime Target position on the replay timeline (seconds).
     */
    void seek(double replayTime) override;

    /**
     * @brief Whether a pipeline exists for the current run.
     *
     * @return True once the pipeline was built; it may still be prerolling (`Preparing`).
     */
    [[nodiscard]] bool isReady() const override { return static_cast<bool>(pipeline_); }

    /**
     * @brief Frames shown since the run was set (for tests and profiling).
     *
     * @return The count of frames handed to the sink (or that would have been, without a sink).
     */
    [[nodiscard]] qint64 framesShown() const { return framesShown_; }

    /**
     * @brief Frames taken from the appsink since the run was set (for tests and profiling).
     *
     * @return The count of pulled samples; some are skipped when late, so it can exceed
     *         `framesShown()`.
     */
    [[nodiscard]] qint64 framesDecoded() const { return framesDecoded_; }

    /**
     * @brief The media time of the frame on screen.
     *
     * @return Media time (seconds); after a seek, the seek's media target until its frame
     *         arrives.
     */
    [[nodiscard]] double frameMediaTime() const { return frameMedia_; }

    /**
     * @brief A piece of the replay timeline: consecutive segments, or a stall.
     */
    struct Span {
        bool stall = false;        ///< a stall: replay time runs, media time does not
        double replayStart = 0.0;  ///< seconds
        double replayEnd = 0.0;    ///< seconds
        double mediaStart = 0.0;   ///< seconds; for a stall, the media time where it occurs
        double mediaEnd = 0.0;     ///< seconds
    };

    /**
     * @brief Splits the replay timeline into spans.
     *
     * Consecutive segment pieces of the clock merge into one span; each stall is its own span.
     * Pieces with no replay duration are left out.
     *
     * @param clock The run's playback clock.
     * @return The spans in replay order; empty if the clock has no piece with a duration.
     */
    [[nodiscard]] static std::vector<Span> makeSpans(const PlaybackClock& clock);

private:
    /**
     * @brief A bus message reduced to plain values, posted from the bus thread to the GUI thread.
     */
    struct BusEvent {
        /**
         * @brief The kind of bus message: preroll done, error or warning.
         */
        enum class Kind { AsyncDone, Error, Warning };
        Kind kind;     ///< what the message was
        QString text;  ///< "element: message" for errors and warnings; empty for AsyncDone
    };

    /**
     * @brief A frame pulled from the appsink, with its media time.
     */
    struct Pending {
        gst::GstSamplePtr sample;  ///< the pulled sample; owns a reference
        double media = 0.0;  ///< the frame's stream time (seconds), 0 if unknown
    };

    /**
     * @brief Stops the bus thread, drops the queued frame, then stops and releases the pipeline.
     *
     * The bus thread is joined first because it posts to this object; resetting the pipeline
     * sets it to NULL, which joins the streaming threads.
     */
    void stopPipeline();

    /**
     * @brief Builds the pipeline for the current run, starts the bus thread and sets PLAYING.
     *
     * Needs the pipeline elements (see `gst::hasPipelineElements()`) and the media file of
     * every played segment. The pipeline stays PLAYING: without sync it only decodes until the
     * appsink queue is full.
     *
     * @param error Receives the reason on failure; must not be null.
     * @return True if the pipeline started. On false, a partly built pipeline may be left
     *         behind; the caller stops it.
     */
    bool buildPipeline(QString* error);

    /**
     * @brief Body of the bus thread: pops async-done, error and warning messages.
     *
     * Runs on the bus thread and never touches `pipeline_`. Each message becomes a `BusEvent`
     * posted to `handleBusEvent()` on the GUI thread; only plain values cross threads. Polls
     * every 100 ms for a stop request.
     *
     * @param stop Stop token of the `std::jthread`.
     * @param bus The pipeline's bus; the thread owns a reference to it.
     */
    void busLoop(const std::stop_token& stop, GstBus* bus);

    /**
     * @brief Handles a bus event on the GUI thread.
     *
     * The first async-done ends the preroll: the state becomes `Paused`, a pending seek (or a
     * seek to 0) is made and a pending play is applied. An error fails the engine; a warning is
     * logged. Ignored once the pipeline is gone.
     *
     * @param event The event posted by `busLoop()`.
     */
    void handleBusEvent(const BusEvent& event);

    /**
     * @brief Takes the next decoded frame from the appsink without waiting.
     *
     * Counts it in `framesDecoded()`.
     *
     * @return The frame and its media time, or `std::nullopt` if none is queued or there is
     *         no appsink.
     */
    [[nodiscard]] std::optional<Pending> pullSample();

    /**
     * @brief Shows a frame in the sink and records its media time.
     *
     * Frames that cannot be wrapped (not raw NV12) are dropped.
     *
     * @param pending The frame to show.
     */
    void show(Pending pending);

    /**
     * @brief Presentation timer step on the GUI thread.
     *
     * Shows the first frame after a seek, then, while running, presents the frame due at the
     * current replay time, switches between `Playing` and `Stalled` at span boundaries,
     * reports the position (at once at a stall's start or end, otherwise about every 30 ms)
     * and sets `Ended` at the replay duration.
     */
    void tick();

    /**
     * @brief Shows the newest queued frame that is due; older due frames are skipped.
     *
     * A frame is due if it starts no later than half a frame after `media`. The first frame
     * that is not due yet is kept for a later tick.
     *
     * @param media The current media time (seconds).
     */
    void presentUpTo(double media);

    /**
     * @brief Shows the first frame near the seek target, discarding frames from before the seek.
     *
     * Frames more than a second from the target are dropped. Clears the wait once a frame is
     * shown; otherwise the next tick tries again.
     */
    void presentFirstAfterSeek();

    /**
     * @brief Whether the replay clock is running.
     *
     * @return True while `Playing` or `Stalled`.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief The current replay time.
     *
     * @return The anchor plus the time elapsed since playing started while running; otherwise
     *         `position()` (seconds).
     */
    [[nodiscard]] double replayNow() const;

    /**
     * @brief Maps a replay time to the media time of the frame to show.
     *
     * Within a stall this is one frame before the stall's media time (the last frame before
     * it); within segments it is clamped to the span's media range.
     *
     * @param replayTime Replay time (seconds).
     * @return Media time (seconds).
     */
    [[nodiscard]] double mediaFor(double replayTime) const;

    /**
     * @brief Finds the span that contains a replay time.
     *
     * @param replayTime Replay time (seconds).
     * @return Index into `spans_` of the last span starting at or before `replayTime`; 0 for
     *         a time before the first span. `spans_` must not be empty.
     */
    [[nodiscard]] std::size_t spanAt(double replayTime) const;

    /**
     * @brief Runs the presentation timer while running or waiting for a seek's frame, and
     *        stops it otherwise.
     */
    void updateTicker();

    /**
     * @brief Stops both timers, sets `Error` and emits `errorOccurred`.
     *
     * @param message What went wrong, for the user.
     */
    void fail(const QString& message);

    std::shared_ptr<const RunData> run_;  ///< replaced only while the pipeline is stopped
    PlaybackClock clock_;                 ///< clock of `run_`; a default clock if there is no run
    std::vector<Span> spans_;             ///< the replay timeline split by `makeSpans()`
    double frameDuration_ = 1.0 / 30.0;  ///< seconds, from the run's frame rate
    QPointer<QVideoSink> sink_;           ///< where frames are shown; not owned, may be null
    QTimer* ticker_;        ///< child: presents frames while playing or waiting for a seek
    QTimer* prerollTimer_;  ///< child: a decoder that never produces a frame is an error
    QElapsedTimer playClock_;   ///< runs while playing; replay time = anchor + elapsed
    double anchor_ = 0.0;       ///< replay time when `playClock_` was started (seconds)
    QElapsedTimer positionClock_;  ///< limits position reports to about 30 per second
    std::optional<Pending> pending_;  ///< the next queued frame, not due yet
    bool awaitingSeekFrame_ = false;  ///< a seek was made and its first frame not shown yet
    double seekMedia_ = 0.0;          ///< media target of the last seek (seconds)
    double frameMedia_ = 0.0;             ///< frame on screen, or the seek target (seconds)
    qint64 framesShown_ = 0;              ///< frames handed to the sink since the run was set
    qint64 framesDecoded_ = 0;            ///< samples pulled since the run was set
    bool prerolled_ = false;              ///< the first async-done has arrived
    bool wantPlay_ = false;               ///< play requested before the preroll
    std::optional<double> pendingSeek_;   ///< seek requested before the preroll

    GstElement* appsink_ = nullptr;  ///< owned by the pipeline
    gst::PipelineGuard pipeline_;    ///< stops the streaming threads, then unrefs
    std::jthread busThread_;         ///< destroyed first
};

} // namespace abrviz
