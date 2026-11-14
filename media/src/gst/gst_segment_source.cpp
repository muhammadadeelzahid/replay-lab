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

constexpr int kTickMs = 5;              // presentation timer while playing
constexpr int kPositionIntervalMs = 30; // position reports while playing
// Some decoders fail without posting an error (VideoToolbox on very small frames); without a
// first picture by then the engine reports an error and the player can fall back.
constexpr int kPrerollTimeoutMs = 8000;
constexpr double kEpsilon = 1e-6;
// A frame this far from a seek's target is from before the seek (normally flushed already).
constexpr double kStaleSeconds = 1.0;

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
    ticker_->setTimerType(Qt::PreciseTimer);
    ticker_->setInterval(kTickMs);
    connect(ticker_, &QTimer::timeout, this, &GstSegmentSource::tick);
}

GstSegmentSource::~GstSegmentSource()
{
    // The bus thread posts to this object; stop it, then the streaming threads. Calls already
    // queued are dropped with the object.
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
    pending_.reset();
    pipeline_.reset();  // NULL state: streaming threads stopped and joined
    appsink_ = nullptr;
}

void GstSegmentSource::setRun(std::shared_ptr<const RunData> run)
{
    stopPipeline();
    ticker_->stop();
    prerollTimer_->stop();
    run_ = std::move(run);
    clock_ = run_ ? PlaybackClock(*run_) : PlaybackClock();
    spans_ = makeSpans(clock_);
    frameDuration_ = run_ && run_->video.frameRate > 0.0 ? 1.0 / run_->video.frameRate : 1.0 / 30.0;
    anchor_ = 0.0;
    awaitingSeekFrame_ = false;
    frameMedia_ = 0.0;
    framesShown_ = 0;
    framesDecoded_ = 0;
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

    // The appsink does not sync: frames are timed by tick(). Its queue of a few frames lets the
    // decoder work ahead and blocks it when full.
    GError* rawError = nullptr;
    GstElement* raw = gst_parse_launch(
        "splitmuxsrc name=source ! decodebin3 ! videoconvert n-threads=4 "
        "! videoscale n-threads=4 add-borders=true "
        "! video/x-raw,format=NV12,width=1920,height=1080,pixel-aspect-ratio=1/1 "
        "! appsink name=sink sync=false max-buffers=8 drop=false enable-last-sample=false",
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

    gst::GstPtr<GstBus> bus(gst_element_get_bus(raw));
    // The thread owns its reference to the bus; it never touches pipeline_.
    busThread_ = std::jthread([this, bus = std::move(bus)](const std::stop_token& stop) {
        busLoop(stop, bus.get());
    });

    // The pipeline stays PLAYING: without sync it only decodes until the appsink queue is full.
    if (gst_element_set_state(raw, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
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
            static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR
                                        | GST_MESSAGE_WARNING)));
        if (!message)
            continue;
        std::optional<BusEvent> event;
        switch (GST_MESSAGE_TYPE(message.get())) {
        case GST_MESSAGE_ASYNC_DONE: event = BusEvent{BusEvent::Kind::AsyncDone, {}}; break;
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
    case BusEvent::Kind::Error:
        fail(event.text);
        break;
    case BusEvent::Kind::Warning:
        qWarning("GStreamer: %s", qPrintable(event.text));
        break;
    }
}

std::optional<GstSegmentSource::Pending> GstSegmentSource::pullSample()
{
    if (!appsink_)
        return std::nullopt;
    gst::GstSamplePtr sample(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_), 0));
    if (!sample)
        return std::nullopt;
    ++framesDecoded_;
    // Media time of the frame: its stream time (splitmuxsrc's time over all the files).
    double media = 0.0;
    GstBuffer* buffer = gst_sample_get_buffer(sample.get());
    const GstSegment* segment = gst_sample_get_segment(sample.get());
    if (buffer && segment && segment->format == GST_FORMAT_TIME && GST_BUFFER_PTS_IS_VALID(buffer)) {
        const guint64 stream = gst_segment_to_stream_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
        if (stream != GST_CLOCK_TIME_NONE)
            media = static_cast<double>(stream) / GST_SECOND;
    }
    return Pending{std::move(sample), media};
}

void GstSegmentSource::show(Pending pending)
{
    QVideoFrame frame = gst::GstVideoBuffer::wrap(std::move(pending.sample));
    if (!frame.isValid())
        return;
    frameMedia_ = pending.media;
    ++framesShown_;
    if (sink_)
        sink_->setVideoFrame(frame);
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

double GstSegmentSource::mediaFor(double replayTime) const
{
    const Span& span = spans_[spanAt(replayTime)];
    if (span.stall)
        return span.mediaStart - frameDuration_;  // the last frame before the stall
    return span.mediaStart + std::clamp(replayTime - span.replayStart, 0.0, span.mediaEnd - span.mediaStart);
}

bool GstSegmentSource::isRunning() const
{
    return state() == State::Playing || state() == State::Stalled;
}

double GstSegmentSource::replayNow() const
{
    if (!isRunning())
        return position();
    return anchor_ + static_cast<double>(playClock_.nsecsElapsed()) / 1e9;
}

void GstSegmentSource::updateTicker()
{
    if (isRunning() || awaitingSeekFrame_)
        ticker_->start();
    else
        ticker_->stop();
}

void GstSegmentSource::seek(double replayTime)
{
    if (!pipeline_)
        return;
    if (!prerolled_) {
        pendingSeek_ = replayTime;
        return;
    }
    replayTime = std::clamp(replayTime, 0.0, clock_.replayDuration());
    if (state() == State::Ended)
        setState(State::Paused);

    const Span& span = spans_[spanAt(replayTime)];
    // Seeking into a stall shows the last frame before it: a moment before its media time.
    const double media = span.stall ? std::max(0.0, span.mediaStart - 0.001) : mediaFor(replayTime);
    pending_.reset();
    gst_element_seek(pipeline_.get(), 1.0, GST_FORMAT_TIME,
                     static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                     GST_SEEK_TYPE_SET, toClock(media),
                     GST_SEEK_TYPE_SET, toClock(clock_.mediaDuration()));
    awaitingSeekFrame_ = true;
    seekMedia_ = media;
    frameMedia_ = media;
    anchor_ = replayTime;
    if (isRunning()) {
        playClock_.start();
        setState(span.stall ? State::Stalled : State::Playing);
    }
    setPosition(replayTime);
    positionClock_.start();
    updateTicker();
}

void GstSegmentSource::play()
{
    if (!pipeline_)
        return;
    if (!prerolled_) {
        wantPlay_ = true;
        return;
    }
    if (isRunning())
        return;
    if (state() == State::Ended || position() >= clock_.replayDuration())
        seek(0.0);
    anchor_ = position();
    playClock_.start();
    positionClock_.start();
    setState(spans_[spanAt(anchor_)].stall ? State::Stalled : State::Playing);
    updateTicker();
}

void GstSegmentSource::pause()
{
    wantPlay_ = false;
    if (!pipeline_ || !isRunning())
        return;
    const double now = std::min(replayNow(), clock_.replayDuration());
    anchor_ = now;
    setState(State::Paused);
    setPosition(now);
    updateTicker();
}

void GstSegmentSource::presentFirstAfterSeek()
{
    while (auto next = pullSample()) {
        if (std::abs(next->media - seekMedia_) > kStaleSeconds)
            continue;  // from before the seek
        awaitingSeekFrame_ = false;
        show(std::move(*next));
        return;
    }
}

void GstSegmentSource::presentUpTo(double media)
{
    // Show the newest queued frame that is due; older due frames are skipped.
    std::optional<Pending> due;
    while (true) {
        if (!pending_)
            pending_ = pullSample();
        if (!pending_)
            break;
        if (pending_->media > media + frameDuration_ / 2.0)
            break;
        due = std::exchange(pending_, std::nullopt);
    }
    if (due)
        show(std::move(*due));
}

void GstSegmentSource::tick()
{
    if (awaitingSeekFrame_)
        presentFirstAfterSeek();
    if (!isRunning()) {
        updateTicker();
        return;
    }
    const double end = clock_.replayDuration();
    const double now = replayNow();
    if (now >= end) {
        if (!awaitingSeekFrame_)
            presentUpTo(clock_.mediaDuration());
        anchor_ = end;
        setPosition(end);
        setState(State::Ended);
        updateTicker();
        return;
    }
    const Span& span = spans_[spanAt(now)];
    const State running = span.stall ? State::Stalled : State::Playing;
    const bool changed = running != state();
    if (!awaitingSeekFrame_)
        presentUpTo(mediaFor(now));
    if (changed)
        setPosition(now);  // the position at a stall's start or end is reported at once
    setState(running);
    if (changed || positionClock_.elapsed() >= kPositionIntervalMs) {
        positionClock_.start();
        setPosition(now);
    }
}

void GstSegmentSource::fail(const QString& message)
{
    prerollTimer_->stop();
    ticker_->stop();
    setState(State::Error);
    emit errorOccurred(message);
}

} // namespace abrviz
