#include "gst_segment_source.h"

#include "gst_ptr.h"
#include "gst_runtime.h"
#include "gst_video_buffer.h"

#include <QFileInfo>
#include <QTimer>
#include <QVideoSink>

#include <gst/app/gstappsink.h>

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

constexpr int kTickMs = 33;
// Some decoders fail without posting an error (VideoToolbox on very small frames); without a
// first picture by then the engine reports an error and the player can fall back.
constexpr int kPrerollTimeoutMs = 8000;
constexpr double kEpsilon = 1e-6;
constexpr double kEosTolerance = 0.25;  // seconds between the last frame and the span's end

gint64 toClock(double seconds)
{
    return std::llround(std::max(0.0, seconds) * static_cast<double>(GST_SECOND));
}

/// The locations `splitmuxsrc` plays, as a NULL-terminated string vector it takes over.
struct Locations {
    std::vector<QByteArray> files;
};

gchar** formatLocation(GstElement*, gpointer data)
{
    const auto* locations = static_cast<const Locations*>(data);
    auto** list = g_new0(gchar*, locations->files.size() + 1);
    for (std::size_t i = 0; i < locations->files.size(); ++i)
        list[i] = g_strdup(locations->files[i].constData());
    return list;
}

void deleteLocations(gpointer data, GClosure*)
{
    delete static_cast<Locations*>(data);
}

QString messageText(GstMessage* message)
{
    GError* raw = nullptr;
    gchar* rawDebug = nullptr;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
        gst_message_parse_error(message, &raw, &rawDebug);
    else
        gst_message_parse_warning(message, &raw, &rawDebug);
    const gst::GErrorPtr error(raw);
    const gst::GCharPtr debug(rawDebug);
    QString text = error ? QString::fromUtf8(error->message) : QStringLiteral("unknown error");
    if (GST_MESSAGE_SRC(message))
        text = QStringLiteral("%1: %2").arg(QString::fromUtf8(GST_OBJECT_NAME(GST_MESSAGE_SRC(message))), text);
    return text;
}

} // namespace

std::vector<GstSegmentSource::Span> GstSegmentSource::makeSpans(const PlaybackClock& clock)
{
    std::vector<Span> spans;
    for (const PlaybackClock::Piece& piece : clock.pieces()) {
        const bool stall = piece.kind == PlaybackClock::Piece::Kind::Stall;
        if (piece.replayEnd - piece.replayStart <= kEpsilon)
            continue;
        if (!stall && !spans.empty() && !spans.back().stall) {
            spans.back().replayEnd = piece.replayEnd;  // consecutive segments play as one span
            spans.back().mediaEnd = piece.mediaEnd;
            continue;
        }
        spans.push_back({stall, piece.replayStart, piece.replayEnd, piece.mediaStart, piece.mediaEnd});
    }
    return spans;
}

GstSegmentSource::GstSegmentSource(QObject* parent)
    : ReplaySource(parent)
    , stallTimer_(new QTimer(this))
    , ticker_(new QTimer(this))
    , prerollTimer_(new QTimer(this))
{
    prerollTimer_->setSingleShot(true);
    prerollTimer_->setInterval(kPrerollTimeoutMs);
    connect(prerollTimer_, &QTimer::timeout, this, [this] {
        if (pipeline_ && !prerolled_) {
            stopPipeline();
            fail(tr("GStreamer could not decode the first segment."));
        }
    });
    stallTimer_->setSingleShot(true);
    stallTimer_->setTimerType(Qt::PreciseTimer);
    connect(stallTimer_, &QTimer::timeout, this, &GstSegmentSource::stallFinished);
    ticker_->setInterval(kTickMs);
    connect(ticker_, &QTimer::timeout, this, &GstSegmentSource::tick);
}

GstSegmentSource::~GstSegmentSource()
{
    // The bus thread and the streaming threads post to this object; stop them first. Deliveries
    // already queued are dropped with the object.
    stopPipeline();
}

QString GstSegmentSource::name() const
{
    return tr("GStreamer");
}

void GstSegmentSource::stopPipeline()
{
    if (busThread_.joinable()) {
        busThread_.request_stop();
        busThread_.join();
    }
    pipeline_.reset();  // NULL state: streaming threads stopped and joined
    appsink_ = nullptr;
    const std::scoped_lock lock(frameMutex_);
    latestFrame_ = {};
    framePending_.store(false, std::memory_order_relaxed);
}

void GstSegmentSource::setRun(std::shared_ptr<const RunData> run)
{
    stopPipeline();
    stallTimer_->stop();
    ticker_->stop();
    prerollTimer_->stop();
    run_ = std::move(run);
    clock_ = run_ ? PlaybackClock(*run_) : PlaybackClock();
    spans_ = makeSpans(clock_);
    span_ = 0;
    stallOffset_ = 0.0;
    frameMedia_ = 0.0;
    framesShown_ = 0;
    prerolled_ = false;
    wantPlay_ = false;
    pendingSeek_.reset();
    setState(State::Idle);
    setPosition(0.0);
    if (!run_ || spans_.empty())
        return;

    QString error;
    if (!buildPipeline(&error)) {
        stopPipeline();
        fail(error);
        return;
    }
    setState(State::Preparing);
    prerollTimer_->start();
}

bool GstSegmentSource::buildPipeline(QString* error)
{
    if (!gst::hasPipelineElements(error))
        return false;

    auto locations = std::make_unique<Locations>();
    for (const PlaybackClock::Piece& piece : clock_.pieces()) {
        if (piece.kind != PlaybackClock::Piece::Kind::Segment)
            continue;
        const auto it = std::ranges::find(run_->segments, piece.segment, &Segment::index);
        if (it == run_->segments.end() || it->mediaPath.isEmpty() || !QFileInfo::exists(it->mediaPath)) {
            *error = tr("The media file of segment %1 is missing.").arg(piece.segment);
            return false;
        }
        locations->files.push_back(QFile::encodeName(it->mediaPath));
    }
    if (locations->files.empty()) {
        *error = tr("The run has no played segments.");
        return false;
    }

    GError* rawError = nullptr;
    GstElement* raw = gst_parse_launch(
        "splitmuxsrc name=source ! decodebin3 ! videoconvert n-threads=4 "
        "! videoscale n-threads=4 add-borders=true "
        "! video/x-raw,format=NV12,width=1920,height=1080,pixel-aspect-ratio=1/1 "
        "! appsink name=sink sync=true max-buffers=2 drop=false enable-last-sample=false",
        &rawError);
    const gst::GErrorPtr parseError(rawError);
    if (!raw) {
        *error = parseError ? QString::fromUtf8(parseError->message) : tr("cannot build the pipeline");
        return false;
    }
    gst_object_ref_sink(raw);
    pipeline_.reset(raw);

    const gst::GstPtr<GstElement> source(gst_bin_get_by_name(GST_BIN(raw), "source"));
    const gst::GstPtr<GstElement> sink(gst_bin_get_by_name(GST_BIN(raw), "sink"));
    appsink_ = sink.get();  // the pipeline keeps it alive
    g_signal_connect_data(source.get(), "format-location", G_CALLBACK(formatLocation),
                          locations.release(), deleteLocations, GConnectFlags{});

    GstAppSinkCallbacks callbacks{};
    callbacks.new_sample = [](GstAppSink* appsink, gpointer self) {
        return static_cast<GstFlowReturn>(
            static_cast<GstSegmentSource*>(self)->pull(GST_ELEMENT(appsink), false));
    };
    callbacks.new_preroll = [](GstAppSink* appsink, gpointer self) {
        return static_cast<GstFlowReturn>(
            static_cast<GstSegmentSource*>(self)->pull(GST_ELEMENT(appsink), true));
    };
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink_), &callbacks, this, nullptr);

    gst::GstPtr<GstBus> bus(gst_element_get_bus(raw));
    // The thread owns its reference to the bus; it never touches pipeline_.
    busThread_ = std::jthread([this, bus = std::move(bus)](const std::stop_token& stop) {
        busLoop(stop, bus.get());
    });

    if (gst_element_set_state(raw, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        *error = tr("The GStreamer pipeline did not start.");
        return false;
    }
    return true;
}

void GstSegmentSource::busLoop(const std::stop_token& stop, GstBus* bus)
{
    while (!stop.stop_requested()) {
        const gst::GstMessagePtr message(gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_EOS
                                        | GST_MESSAGE_ERROR | GST_MESSAGE_WARNING)));
        if (!message)
            continue;
        std::optional<BusEvent> event;
        switch (GST_MESSAGE_TYPE(message.get())) {
        case GST_MESSAGE_ASYNC_DONE: event = BusEvent{BusEvent::Kind::AsyncDone, {}}; break;
        case GST_MESSAGE_EOS: event = BusEvent{BusEvent::Kind::Eos, {}}; break;
        case GST_MESSAGE_ERROR: event = BusEvent{BusEvent::Kind::Error, messageText(message.get())}; break;
        case GST_MESSAGE_WARNING: event = BusEvent{BusEvent::Kind::Warning, messageText(message.get())}; break;
        default: break;
        }
        // Only plain values cross to the GUI thread, never the GstMessage.
        if (event)
            QMetaObject::invokeMethod(this, [this, e = *event] { handleBusEvent(e); },
                                      Qt::QueuedConnection);
    }
}

void GstSegmentSource::handleBusEvent(const BusEvent& event)
{
    if (!pipeline_)
        return;
    switch (event.kind) {
    case BusEvent::Kind::AsyncDone:
        if (!prerolled_) {
            prerolled_ = true;
            prerollTimer_->stop();
            setState(State::Paused);
            const double target = pendingSeek_.value_or(0.0);
            pendingSeek_.reset();
            seek(target);
            if (std::exchange(wantPlay_, false))
                play();
        }
        break;
    case BusEvent::Kind::Eos:
        // An EOS posted before a seek can arrive after it; only one at the span's end counts.
        if (state() == State::Playing && spans_[span_].mediaEnd - frameMedia_ < kEosTolerance)
            spanFinished();
        break;
    case BusEvent::Kind::Error:
        fail(event.text);
        break;
    case BusEvent::Kind::Warning:
        qWarning("GStreamer: %s", qPrintable(event.text));
        break;
    }
}

int GstSegmentSource::pull(GstElement* appsink, bool preroll)
{
    GstAppSink* sink = GST_APP_SINK(appsink);
    gst::GstSamplePtr sample(preroll ? gst_app_sink_pull_preroll(sink) : gst_app_sink_pull_sample(sink));
    if (!sample)
        return GST_FLOW_EOS;
    // Media time of the frame: its stream time (splitmuxsrc's time over all the files).
    double media = 0.0;
    GstBuffer* buffer = gst_sample_get_buffer(sample.get());
    const GstSegment* segment = gst_sample_get_segment(sample.get());
    if (buffer && segment && GST_BUFFER_PTS_IS_VALID(buffer)) {
        const guint64 stream = gst_segment_to_stream_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
        if (stream != GST_CLOCK_TIME_NONE)
            media = static_cast<double>(stream) / GST_SECOND;
    }
    QVideoFrame frame = gst::GstVideoBuffer::wrap(std::move(sample));
    if (!frame.isValid())
        return GST_FLOW_OK;
    {
        const std::scoped_lock lock(frameMutex_);
        latestFrame_ = std::move(frame);
        latestMedia_ = media;
    }
    // At most one delivery is queued; it takes whatever frame is newest when it runs.
    if (!framePending_.exchange(true, std::memory_order_acq_rel))
        QMetaObject::invokeMethod(this, &GstSegmentSource::deliverFrame, Qt::QueuedConnection);
    return GST_FLOW_OK;
}

void GstSegmentSource::deliverFrame()
{
    QVideoFrame frame;
    double media = 0.0;
    {
        const std::scoped_lock lock(frameMutex_);
        framePending_.store(false, std::memory_order_release);
        frame = std::exchange(latestFrame_, QVideoFrame());
        media = latestMedia_;
    }
    if (!frame.isValid())
        return;
    frameMedia_ = media;
    ++framesShown_;
    if (sink_)
        sink_->setVideoFrame(frame);
    if (state() == State::Playing)
        tick();
}

void GstSegmentSource::setVideoSink(QVideoSink* sink)
{
    sink_ = sink;
}

std::size_t GstSegmentSource::spanAt(double replayTime) const
{
    const auto it = std::ranges::upper_bound(spans_, replayTime + kEpsilon, {}, &Span::replayStart);
    return it == spans_.begin() ? 0 : static_cast<std::size_t>(std::distance(spans_.begin(), it) - 1);
}

void GstSegmentSource::seekMedia(double start, double stop)
{
    {
        // Frames from before the seek must not be shown after it.
        const std::scoped_lock lock(frameMutex_);
        latestFrame_ = {};
    }
    frameMedia_ = start;
    gst_element_seek(pipeline_.get(), 1.0, GST_FORMAT_TIME,
                     static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                     GST_SEEK_TYPE_SET, toClock(start), GST_SEEK_TYPE_SET, toClock(stop));
}

void GstSegmentSource::seek(double replayTime)
{
    if (!pipeline_)
        return;
    if (!prerolled_) {
        pendingSeek_ = replayTime;
        return;
    }
    stallTimer_->stop();
    const double end = clock_.replayDuration();
    replayTime = std::clamp(replayTime, 0.0, end);
    const bool playing = state() == State::Playing || state() == State::Stalled;
    if (state() == State::Ended)
        setState(State::Paused);

    span_ = spanAt(replayTime);
    const Span& span = spans_[span_];
    const double offset = std::clamp(replayTime - span.replayStart, 0.0, span.replayEnd - span.replayStart);
    if (span.stall) {
        // Show the last frame before the stall: a moment before the stall's media time.
        stallOffset_ = offset;
        const double at = std::max(0.0, span.mediaStart - 0.001);
        seekMedia(at, span.mediaStart);
        frameMedia_ = span.mediaStart;
        if (playing)
            startStall(offset);
    } else {
        seekMedia(span.mediaStart + offset, span.mediaEnd);
        if (playing)
            enterSpan(span_, offset);  // from a stall the pipeline is paused: play on
    }
    setPosition(replayTime);
}

void GstSegmentSource::play()
{
    if (!pipeline_)
        return;
    if (!prerolled_) {
        wantPlay_ = true;
        return;
    }
    if (state() == State::Playing || state() == State::Stalled)
        return;
    if (state() == State::Ended)
        seek(0.0);
    enterSpan(span_, position() - spans_[span_].replayStart);
}

void GstSegmentSource::enterSpan(std::size_t index, double offset)
{
    span_ = index;
    const Span& span = spans_[index];
    if (span.stall) {
        startStall(offset);
        return;
    }
    gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING);
    setState(State::Playing);
    ticker_->start();
}

void GstSegmentSource::startStall(double offset)
{
    const Span& span = spans_[span_];
    stallOffset_ = offset;
    gst_element_set_state(pipeline_.get(), GST_STATE_PAUSED);
    stallClock_.start();
    const double remaining = std::max(0.0, span.replayEnd - span.replayStart - offset);
    stallTimer_->start(static_cast<int>(std::lround(remaining * 1000.0)));
    setState(State::Stalled);
    ticker_->start();
}

void GstSegmentSource::stallFinished()
{
    if (state() != State::Stalled)
        return;
    const std::size_t next = span_ + 1;
    if (next >= spans_.size()) {
        ticker_->stop();
        setPosition(clock_.replayDuration());
        setState(State::Ended);
        return;
    }
    span_ = next;
    const Span& span = spans_[next];
    seekMedia(span.mediaStart, span.mediaEnd);
    setPosition(span.replayStart);
    enterSpan(next, 0.0);
}

void GstSegmentSource::spanFinished()
{
    const std::size_t next = span_ + 1;
    if (next >= spans_.size()) {
        ticker_->stop();
        gst_element_set_state(pipeline_.get(), GST_STATE_PAUSED);
        setPosition(clock_.replayDuration());
        setState(State::Ended);
        return;
    }
    span_ = next;
    setPosition(spans_[next].replayStart);
    enterSpan(next, 0.0);
}

void GstSegmentSource::pause()
{
    wantPlay_ = false;
    if (!pipeline_)
        return;
    if (state() == State::Stalled) {
        stallTimer_->stop();
        stallOffset_ += static_cast<double>(stallClock_.elapsed()) / 1000.0;
        setPosition(spans_[span_].replayStart + stallOffset_);
    } else if (state() == State::Playing) {
        gst_element_set_state(pipeline_.get(), GST_STATE_PAUSED);
        tick();
    } else {
        return;
    }
    ticker_->stop();
    setState(State::Paused);
}

void GstSegmentSource::tick()
{
    if (spans_.empty())
        return;
    const Span& span = spans_[span_];
    double replay = 0.0;
    if (state() == State::Stalled) {
        replay = span.replayStart + stallOffset_ + static_cast<double>(stallClock_.elapsed()) / 1000.0;
    } else if (state() == State::Playing) {
        replay = span.replayStart + (frameMedia_ - span.mediaStart);
    } else {
        return;
    }
    setPosition(std::clamp(replay, span.replayStart, span.replayEnd));
}

void GstSegmentSource::fail(const QString& message)
{
    prerollTimer_->stop();
    stallTimer_->stop();
    ticker_->stop();
    setState(State::Error);
    emit errorOccurred(message);
}

} // namespace abrviz
