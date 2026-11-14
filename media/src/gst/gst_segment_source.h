#pragma once

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

/// Plays the run's segment files directly with GStreamer:
///
///     splitmuxsrc ! decodebin3 ! videoconvert ! videoscale
///                 ! video/x-raw,format=NV12,width=1920,height=1080 ! appsink sync=false
///
/// `splitmuxsrc` reads the played segments, in order and at their played rungs, as one stream
/// whose time is the media time of the `PlaybackClock`.
///
/// Timing is done here, not by the sink. The appsink does not sync to a clock; it keeps a few
/// decoded frames queued (the streaming threads block when it is full). While playing, a timer
/// on the GUI thread advances a replay clock, maps it to media time through the spans of the
/// replay timeline (runs of segments, and stalls), and shows the newest queued frame that is due.
/// A stall holds the last frame while the replay clock runs on; playback ends at the replay
/// duration, which is where a cut-off run stopped. Seeks are flushing and accurate; the first
/// frame after a seek is shown at once, also while paused.
///
/// Threads: GStreamer's streaming threads only decode into the appsink queue, which this
/// object reads on the GUI thread. A `std::jthread` pops bus messages and posts plain values to
/// the GUI thread. The destructor stops the bus thread and then the pipeline.
class GstSegmentSource final : public ReplaySource {
    Q_OBJECT
    Q_PROPERTY(qint64 framesDecoded READ framesDecoded)
    Q_PROPERTY(qint64 framesShown READ framesShown)

public:
    explicit GstSegmentSource(QObject* parent = nullptr);
    ~GstSegmentSource() override;

    GstSegmentSource(const GstSegmentSource&) = delete;
    GstSegmentSource& operator=(const GstSegmentSource&) = delete;
    GstSegmentSource(GstSegmentSource&&) = delete;
    GstSegmentSource& operator=(GstSegmentSource&&) = delete;

    [[nodiscard]] QString name() const override;
    void setRun(std::shared_ptr<const RunData> run) override;
    void setVideoSink(QVideoSink* sink) override;
    void play() override;
    void pause() override;
    void seek(double replayTime) override;
    [[nodiscard]] bool isReady() const override { return static_cast<bool>(pipeline_); }

    /// Frames shown since the run was set (for tests and profiling).
    [[nodiscard]] qint64 framesShown() const { return framesShown_; }
    /// Frames taken from the appsink since the run was set (some are skipped when late).
    [[nodiscard]] qint64 framesDecoded() const { return framesDecoded_; }
    /// The media time of the frame on screen.
    [[nodiscard]] double frameMediaTime() const { return frameMedia_; }

    /// A piece of the replay timeline: consecutive segments, or a stall.
    struct Span {
        bool stall = false;
        double replayStart = 0.0;
        double replayEnd = 0.0;
        double mediaStart = 0.0;
        double mediaEnd = 0.0;
    };
    [[nodiscard]] static std::vector<Span> makeSpans(const PlaybackClock& clock);

private:
    struct BusEvent {
        enum class Kind { AsyncDone, Error, Warning };
        Kind kind;
        QString text;
    };
    struct Pending {
        gst::GstSamplePtr sample;
        double media = 0.0;
    };

    void stopPipeline();
    bool buildPipeline(QString* error);
    void busLoop(const std::stop_token& stop, GstBus* bus);
    void handleBusEvent(const BusEvent& event);
    [[nodiscard]] std::optional<Pending> pullSample();
    void show(Pending pending);
    void tick();
    void presentUpTo(double media);
    void presentFirstAfterSeek();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] double replayNow() const;
    [[nodiscard]] double mediaFor(double replayTime) const;
    [[nodiscard]] std::size_t spanAt(double replayTime) const;
    void updateTicker();
    void fail(const QString& message);

    std::shared_ptr<const RunData> run_;  // replaced only while the pipeline is stopped
    PlaybackClock clock_;
    std::vector<Span> spans_;
    double frameDuration_ = 1.0 / 30.0;
    QPointer<QVideoSink> sink_;
    QTimer* ticker_;        // child: presents frames while playing or waiting for a seek
    QTimer* prerollTimer_;  // child: a decoder that never produces a frame is an error
    QElapsedTimer playClock_;   // runs while playing; replay time = anchor + elapsed
    double anchor_ = 0.0;
    QElapsedTimer positionClock_;  // limits position reports to about 30 per second
    std::optional<Pending> pending_;  // the next queued frame, not due yet
    bool awaitingSeekFrame_ = false;
    double seekMedia_ = 0.0;
    double frameMedia_ = 0.0;
    qint64 framesShown_ = 0;
    qint64 framesDecoded_ = 0;
    bool prerolled_ = false;
    bool wantPlay_ = false;
    std::optional<double> pendingSeek_;

    GstElement* appsink_ = nullptr;  // owned by the pipeline
    gst::PipelineGuard pipeline_;    // stops the streaming threads, then unrefs
    std::jthread busThread_;         // destroyed first
};

} // namespace abrviz
