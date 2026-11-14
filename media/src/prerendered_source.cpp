#include "abrviz/media/prerendered_source.h"

#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>

#include <cmath>
#include <tuple>

namespace abrviz {

PrerenderedSource::PrerenderedSource(RenderQueue* queue, QObject* parent)
    : ReplaySource(parent)
    , queue_(queue)
    , player_(new QMediaPlayer(this))
    , watcher_(new QFutureWatcher<RenderResult>(this))
{
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        setPosition(static_cast<double>(ms) / 1000.0);
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
            &PrerenderedSource::mediaStatusChanged);
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            &PrerenderedSource::playbackStateChanged);
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) {
                setState(State::Error);
                emit errorOccurred(message);
            });
    connect(watcher_, &QFutureWatcherBase::finished, this, &PrerenderedSource::renderFinished);
    connect(watcher_, &QFutureWatcherBase::progressValueChanged, this, [this](int value) {
        const int maximum = watcher_->progressMaximum();
        if (maximum > 0)
            emit preparationProgress(static_cast<double>(value) / maximum);
    });
}

PrerenderedSource::~PrerenderedSource()
{
    // The render guard cancels the job and waits for it; no result may arrive meanwhile.
    watcher_->disconnect(this);
    player_->disconnect(this);
    player_->stop();
}

QString PrerenderedSource::name() const
{
    return tr("Pre-rendered");
}

void PrerenderedSource::setRun(std::shared_ptr<const RunData> run)
{
    render_.reset();  // cancels a render of the previous run and waits for it
    player_->stop();
    player_->setSource(QUrl());
    file_.clear();
    pendingSeek_.reset();
    playWhenReady_ = false;
    run_ = std::move(run);
    setState(State::Idle);
    setPosition(0.0);
    if (!run_)
        return;
    if (const auto cached = queue_->cachedFile(*run_))
        load(*cached);
}

void PrerenderedSource::setVideoSink(QVideoSink* sink)
{
    sink_ = sink;
    player_->setVideoSink(sink);
}

void PrerenderedSource::prepare()
{
    if (!run_ || isReady() || render_.isRunning())
        return;
    auto future = queue_->enqueue({run_});
    render_.reset(future);
    watcher_->setFuture(future);
    setState(State::Preparing);
    emit preparationProgress(0.0);
}

void PrerenderedSource::renderFinished()
{
    const auto future = watcher_->future();
    std::ignore = render_.release();
    if (future.isCanceled() || future.resultCount() == 0) {
        setState(State::Idle);
        return;
    }
    const RenderResult result = future.result();
    if (!result.ok()) {
        setState(State::Error);
        emit errorOccurred(tr("Rendering failed: %1").arg(result.error));
        return;
    }
    emit preparationProgress(1.0);
    load(result.path);
}

void PrerenderedSource::load(const QString& file)
{
    file_ = file;
    setState(State::Preparing);
    player_->setSource(QUrl::fromLocalFile(file));
}

void PrerenderedSource::mediaStatusChanged()
{
    switch (player_->mediaStatus()) {
    case QMediaPlayer::LoadedMedia:
        if (state() == State::Preparing) {
            setState(State::Paused);
            if (pendingSeek_)
                seek(*std::exchange(pendingSeek_, std::nullopt));
            if (playWhenReady_)
                play();
        }
        break;
    case QMediaPlayer::EndOfMedia:
        setState(State::Ended);
        break;
    case QMediaPlayer::InvalidMedia:
        setState(State::Error);
        emit errorOccurred(tr("Cannot play %1").arg(file_));
        break;
    default:
        break;
    }
}

void PrerenderedSource::playbackStateChanged()
{
    if (scrubbing_ || state() == State::Preparing || state() == State::Error)
        return;
    switch (player_->playbackState()) {
    case QMediaPlayer::PlayingState:
        setState(State::Playing);
        break;
    case QMediaPlayer::PausedState:
        setState(State::Paused);
        break;
    case QMediaPlayer::StoppedState:
        if (player_->mediaStatus() == QMediaPlayer::EndOfMedia)
            setState(State::Ended);
        break;
    }
}

void PrerenderedSource::play()
{
    finishScrub();
    if (!isReady() || state() == State::Preparing) {
        playWhenReady_ = true;
        return;
    }
    if (state() == State::Ended)
        player_->setPosition(0);
    player_->play();
}

void PrerenderedSource::pause()
{
    playWhenReady_ = false;
    finishScrub();
    if (isReady())
        player_->pause();
}

void PrerenderedSource::seek(double replayTime)
{
    if (!isReady() || state() == State::Preparing) {
        pendingSeek_ = replayTime;
        return;
    }
    if (state() == State::Ended)
        setState(State::Paused);
    const auto ms = static_cast<qint64>(std::llround(replayTime * 1000.0));
    player_->setPosition(ms);
    if (state() == State::Paused)
        showFrameAt(ms);
}

void PrerenderedSource::showFrameAt(qint64 ms)
{
    scrubTarget_ = ms;
    if (scrubbing_ || !sink_)
        return;  // the running scrub picks up the new target
    scrubbing_ = true;
    scrubConnection_ = connect(sink_.data(), &QVideoSink::videoFrameChanged, this,
                               &PrerenderedSource::finishScrub);
    QTimer::singleShot(500, this, &PrerenderedSource::finishScrub);  // no frame: give up
    player_->play();
}

void PrerenderedSource::finishScrub()
{
    if (!scrubbing_)
        return;
    scrubbing_ = false;
    disconnect(scrubConnection_);
    player_->pause();
    player_->setPosition(scrubTarget_);
}

} // namespace abrviz
