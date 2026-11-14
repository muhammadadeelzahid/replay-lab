#pragma once

#include "pipeline_guard.h"

#include "abrviz/core/playback/playback_clock.h"
#include "abrviz/media/replay_source.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QVideoFrame>

#include <atomic>
#include <mutex>
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
///                 ! video/x-raw,format=NV12,width=1920,height=1080 ! appsink
///
/// `splitmuxsrc` reads the played segments, in order and at their played rungs, as one stream
/// whose time is the media time of the `PlaybackClock`. The replay timeline is cut into spans:
/// runs of segments between stalls, and the stalls. A segment span is played with a seek whose
/// stop is the span's end; at its end (EOS) the engine holds the last frame for the stall's
/// duration and then seeks to the next span. Cut-off playback ends at the stop of the last span.
///
/// Threads: GStreamer's streaming threads only fill the frame slot and post one queued delivery
/// at a time; a `std::jthread` pops bus messages and posts them as queued calls. Everything
/// else runs on the GUI thread. The destructor stops the bus thread and the pipeline before the
/// members they use go away.
class GstSegmentSource final : public ReplaySource {
    Q_OBJECT

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
        enum class Kind { AsyncDone, Eos, Error, Warning };
        Kind kind;
        QString text;
    };

    void stopPipeline();
    bool buildPipeline(QString* error);
    void busLoop(const std::stop_token& stop, GstBus* bus);
    void handleBusEvent(const BusEvent& event);
    void frameArrived(QVideoFrame frame, double mediaTime);
    void deliverFrame();
    void seekMedia(double start, double stop);
    void enterSpan(std::size_t index, double offset);
    void startStall(double offset);
    void stallFinished();
    void spanFinished();
    void tick();
    [[nodiscard]] std::size_t spanAt(double replayTime) const;
    void fail(const QString& message);

    /// Streaming thread: takes the sample from the appsink into the frame slot.
    int pull(GstElement* appsink, bool preroll);

    // GUI thread.
    std::shared_ptr<const RunData> run_;  // replaced only while the pipeline is stopped
    PlaybackClock clock_;
    std::vector<Span> spans_;
    std::size_t span_ = 0;
    QPointer<QVideoSink> sink_;
    QTimer* stallTimer_;  // child
    QTimer* ticker_;      // child
    QTimer* prerollTimer_;  // child: a decoder that never produces a frame is an error
    QElapsedTimer stallClock_;
    double stallOffset_ = 0.0;
    double frameMedia_ = 0.0;
    qint64 framesShown_ = 0;
    bool prerolled_ = false;
    bool wantPlay_ = false;
    std::optional<double> pendingSeek_;

    // Shared with the streaming threads.
    std::mutex frameMutex_;
    QVideoFrame latestFrame_;
    double latestMedia_ = 0.0;
    std::atomic<bool> framePending_{false};

    GstElement* appsink_ = nullptr;  // owned by the pipeline
    gst::PipelineGuard pipeline_;    // stops the streaming threads, then unrefs
    std::jthread busThread_;         // destroyed first
};

} // namespace abrviz
