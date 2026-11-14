#include "replay_player.h"

#include "format.h"
#include "playback_activity.h"
#include "session.h"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace abrviz {

namespace {

constexpr double kVideoWidth = 1920.0;
constexpr double kVideoHeight = 1080.0;
constexpr double kOverlayMargin = 24.0;

const BufferPoint* bufferAt(const RunData& run, double time)
{
    const auto it = std::ranges::upper_bound(run.buffer, time, {}, &BufferPoint::time);
    return it == run.buffer.begin() ? nullptr : &*std::prev(it);
}

} // namespace

ReplayPlayer::ReplayPlayer(Session* session, RenderQueue* queue, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , queue_(queue)
    , scene_(new QGraphicsScene(this))
    , view_(new QGraphicsView(scene_, this))
    , video_(new QGraphicsVideoItem())
    , overlayBox_(new QGraphicsRectItem())
    , overlay_(new QGraphicsTextItem())
    , message_(new QGraphicsTextItem())
    , renderButton_(new QPushButton(tr("Render Video"), this))
    , progress_(new QProgressBar(this))
    , renderBar_(new QWidget(this))
{
    setObjectName(QStringLiteral("replayPlayer"));

    // The scene owns its items.
    scene_->setSceneRect(0, 0, kVideoWidth, kVideoHeight);
    scene_->setBackgroundBrush(Qt::black);
    video_->setSize({kVideoWidth, kVideoHeight});
    video_->setAspectRatioMode(Qt::KeepAspectRatio);
    scene_->addItem(video_);

    // Overlay and message are drawn in screen pixels, not scene units, so they stay readable
    // however small the player is.
    overlayBox_->setBrush(QColor(0, 0, 0, 160));
    overlayBox_->setPen(Qt::NoPen);
    overlayBox_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    overlayBox_->setPos(kOverlayMargin, kOverlayMargin);
    overlayBox_->setZValue(1);
    scene_->addItem(overlayBox_);
    QFont font = overlay_->font();
    font.setPixelSize(13);
    overlay_->setFont(font);
    overlay_->setDefaultTextColor(Qt::white);
    overlay_->setParentItem(overlayBox_);  // owned by the box from here on
    overlay_->setPos(6, 3);

    font.setPixelSize(16);
    message_->setFont(font);
    message_->setDefaultTextColor(QColor(230, 230, 230));
    message_->setTextWidth(420);
    message_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    message_->setPos(kVideoWidth / 2, kVideoHeight / 2);
    message_->setZValue(3);
    scene_->addItem(message_);

    view_->setFrameShape(QFrame::NoFrame);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setBackgroundBrush(Qt::black);
    view_->setMinimumSize(320, 180);

    progress_->setMaximumWidth(200);
    progress_->setRange(0, 1000);
    progress_->setTextVisible(false);
    progress_->hide();
    connect(renderButton_, &QPushButton::clicked, this, &ReplayPlayer::prepare);

    // Shown only while the pre-rendered engine has no video yet.
    auto* controls = new QHBoxLayout(renderBar_);
    controls->setContentsMargins(8, 4, 8, 4);
    controls->addStretch(1);
    controls->addWidget(renderButton_);
    controls->addWidget(progress_);
    controls->addStretch(1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(view_, 1);
    layout->addWidget(renderBar_);

    connect(session_, &Session::runChanged, this, &ReplayPlayer::runChanged);
    connect(session_->cursor(), &TimeCursor::timeChanged, this, &ReplayPlayer::cursorMoved);
    runChanged();
}

ReplayPlayer::~ReplayPlayer()
{
    releaseEngine();
}

bool ReplayPlayer::setEngine(Engine engine)
{
    // The old engine goes first: it may hold a render job or a pipeline.
    releaseEngine();
    engine_ = ReplaySourceFactory::create(engine, queue_);
    if (!engine_) {
        updateControls();
        return false;
    }
    engineKind_ = engine;
    error_.clear();
    engineShowedPicture_ = false;
    engine_->setVideoSink(video_->videoSink());
    connect(engine_.get(), &ReplaySource::positionChanged, this,
            &ReplayPlayer::enginePositionChanged);
    connect(engine_.get(), &ReplaySource::stateChanged, this, &ReplayPlayer::engineStateChanged);
    connect(engine_.get(), &ReplaySource::errorOccurred, this, [this](const QString& message) {
        error_ = message;
        updateMessage();
        updateControls();
        // GStreamer could not show this run at all: use the pre-rendered engine instead. Not
        // from inside the engine's own signal, which may be emitted from its setRun().
        if (engineKind_ == Engine::GStreamer && !engineShowedPicture_) {
            QMetaObject::invokeMethod(this, [this, message] {
                if (engineKind_ != Engine::GStreamer || engineShowedPicture_)
                    return;
                setEngine(Engine::Prerendered);
                emit engineFellBack(message);
            }, Qt::QueuedConnection);
        }
    });
    connect(engine_.get(), &ReplaySource::preparationProgress, this, [this](double fraction) {
        progress_->setValue(static_cast<int>(fraction * 1000.0));
    });
    engine_->setRun(session_->run());
    if (engine_->isReady())
        engine_->seek(session_->clock().simToReplay(session_->cursor()->time()));
    emit engineChanged();
    updateOverlay();
    updateMessage();
    updateControls();
    return true;
}

void ReplayPlayer::releaseEngine()
{
    if (!engine_)
        return;
    engine_->disconnect(this);
    engine_.reset();  // cancels and waits for its render job, stops playback
    activity_.reset();
    engineKind_.reset();
    emit engineChanged();
}

void ReplayPlayer::setOverlayVisible(bool visible)
{
    overlayVisible_ = visible;
    updateOverlay();
}

QVideoSink* ReplayPlayer::videoSink() const
{
    return video_->videoSink();
}

QString ReplayPlayer::overlayText() const
{
    return overlay_->toPlainText();
}

QString ReplayPlayer::messageText() const
{
    return message_->isVisible() ? message_->toPlainText() : QString();
}

bool ReplayPlayer::isRunning() const
{
    return engine_
        && (engine_->state() == ReplaySource::State::Playing
            || engine_->state() == ReplaySource::State::Stalled);
}

void ReplayPlayer::togglePlay()
{
    if (isRunning())
        pause();
    else
        play();
}

void ReplayPlayer::play()
{
    if (!engine_ || !engine_->isReady())
        return;
    const double end = session_->clock().replayDuration();
    if (engine_->state() == ReplaySource::State::Ended || engine_->position() >= end - 0.05)
        engine_->seek(0.0);
    engine_->play();
}

void ReplayPlayer::pause()
{
    if (engine_)
        engine_->pause();
}

void ReplayPlayer::prepare()
{
    if (!engine_)
        return;
    error_.clear();
    engine_->prepare();
    updateMessage();
    updateControls();
}

void ReplayPlayer::runChanged()
{
    error_.clear();
    engineShowedPicture_ = false;
    if (engine_)
        engine_->setRun(session_->run());
    updateOverlay();
    updateMessage();
    updateControls();
}

void ReplayPlayer::cursorMoved(double time, TimeCursor::Source source)
{
    if (source != TimeCursor::Source::Player && engine_ && engine_->isReady())
        engine_->seek(session_->clock().simToReplay(time));
    updateOverlay();
}

void ReplayPlayer::enginePositionChanged(double replayTime)
{
    // Only a running engine moves the cursor; a seek reports the position it was asked for.
    if (isRunning())
        session_->cursor()->setTime(session_->clock().replayToSim(replayTime),
                                    TimeCursor::Source::Player);
    updateControls();
}

void ReplayPlayer::engineStateChanged(ReplaySource::State state)
{
    if (state == ReplaySource::State::Paused || state == ReplaySource::State::Playing)
        engineShowedPicture_ = true;
    if (isRunning() && !activity_)
        activity_ = std::make_unique<PlaybackActivity>();
    else if (!isRunning())
        activity_.reset();
    // Once ready, show the picture at the cursor.
    if (state == ReplaySource::State::Paused && engine_->position() == 0.0 && session_->run())
        engine_->seek(session_->clock().simToReplay(session_->cursor()->time()));
    updateOverlay();
    updateMessage();
    updateControls();
    emit stateChanged(state);
}

void ReplayPlayer::updateOverlay()
{
    const auto& run = session_->run();
    if (!run) {
        overlay_->hide();
        overlayBox_->hide();
        return;
    }
    const double time = session_->cursor()->time();
    const PlaybackClock::Position position = session_->clock().positionAtSim(time);
    QStringList lines;
    if (position.beforeStart) {
        lines << tr("Before playback starts (startup %1)")
                     .arg(format::seconds(run->playbackStart - run->streamStart));
    } else {
        QString segment = tr("Segment %1").arg(position.segment);
        const auto it = std::ranges::find(run->segments, position.segment, &Segment::index);
        if (it != run->segments.end()) {
            if (const Rung* rung = run->video.rung(it->playedRung)) {
                segment += tr("  ·  rung %1  ·  %2 x %3  ·  %4")
                               .arg(rung->index)
                               .arg(rung->width)
                               .arg(rung->height)
                               .arg(format::bitrate(static_cast<double>(rung->bitrate)));
            }
        }
        lines << segment;
        QString status;
        if (const BufferPoint* buffer = bufferAt(*run, time); buffer && !position.afterEnd)
            status = tr("Buffer %1").arg(format::seconds(buffer->level));
        if (position.stalled)
            status += tr("  ·  STALLED");
        else if (position.afterEnd)
            status = run->playbackCutOff() ? tr("Playback cut off: the simulation stopped")
                                           : tr("Playback ended");
        lines << status;
    }
    lines << tr("%1  ·  replay %2  ·  simulation %3")
                 .arg(engine_ ? engine_->name() : tr("no engine"),
                      format::seconds(session_->clock().simToReplay(time)),
                      format::seconds(time));
    overlay_->setPlainText(lines.join(QLatin1Char('\n')));
    const QRectF text = overlay_->boundingRect();
    overlayBox_->setRect(0, 0, text.width() + 12, text.height() + 6);
    overlay_->setVisible(overlayVisible_);
    overlayBox_->setVisible(overlayVisible_);
}

void ReplayPlayer::updateMessage()
{
    QString text;
    if (!session_->run()) {
        text = tr("Open a folder of bundles, then a run.");
    } else if (!engine_) {
        text = tr("No replay engine.");
    } else if (!error_.isEmpty()) {
        text = error_;
    } else if (engine_->state() == ReplaySource::State::Preparing && !engine_->isReady()) {
        text = tr("Rendering the video this viewer saw ...");
    } else if (!engine_->isReady() && engineKind_ != Engine::Prerendered) {
        text = tr("This run has no played segments to show.");
    } else if (!engine_->isReady()) {
        text = tr("The %1 engine plays a video rendered from the played segments, with the stalls "
                  "as frozen frames. Render it with Playback, Render Video (it uses every core "
                  "and is kept in the cache).")
                   .arg(engine_->name().toLower());
    }
    message_->setPlainText(text);
    message_->setVisible(!text.isEmpty());
    // Centred on the middle of the video, in screen pixels.
    const QRectF bounds = message_->boundingRect();
    message_->setTransform(QTransform::fromTranslate(-bounds.width() / 2, -bounds.height() / 2));
}

void ReplayPlayer::updateControls()
{
    const bool ready = engine_ && engine_->isReady() && session_->run();
    const bool preparing = engine_ && engine_->state() == ReplaySource::State::Preparing
        && !engine_->isReady();
    Q_UNUSED(ready);
    // Only the pre-rendered engine has something to prepare.
    const bool needsRender = engineKind_ == Engine::Prerendered && session_->run() && !engine_->isReady();
    renderBar_->setVisible(needsRender);
    renderButton_->setVisible(!preparing);
    progress_->setVisible(preparing);
}

void ReplayPlayer::fitVideo()
{
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void ReplayPlayer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    fitVideo();
}

void ReplayPlayer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    fitVideo();
}

} // namespace abrviz
