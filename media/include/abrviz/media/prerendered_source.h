#pragma once

#include "abrviz/core/render/render_queue.h"
#include "abrviz/core/util/future_guard.h"
#include "abrviz/media/replay_source.h"

#include <QFutureWatcher>
#include <QPointer>

#include <optional>

class QMediaPlayer;

namespace abrviz {

/// Plays the run's `experienced.mp4` through `QMediaPlayer`. The file is made by the
/// `RenderQueue`; if it is in the cache it is used at once, otherwise `prepare()` renders it.
/// Stalls are frozen frames in the file and cut-off playback ends where it stopped, so the
/// file's time is the replay time.
class PrerenderedSource final : public ReplaySource {
    Q_OBJECT

public:
    /// `queue` must outlive the source.
    explicit PrerenderedSource(RenderQueue* queue, QObject* parent = nullptr);
    ~PrerenderedSource() override;

    PrerenderedSource(const PrerenderedSource&) = delete;
    PrerenderedSource& operator=(const PrerenderedSource&) = delete;
    PrerenderedSource(PrerenderedSource&&) = delete;
    PrerenderedSource& operator=(PrerenderedSource&&) = delete;

    [[nodiscard]] QString name() const override;
    void setRun(std::shared_ptr<const RunData> run) override;
    void setVideoSink(QVideoSink* sink) override;
    void play() override;
    void pause() override;
    void seek(double replayTime) override;
    [[nodiscard]] bool isReady() const override { return !file_.isEmpty(); }
    void prepare() override;

    /// The file being played (empty until it is ready).
    [[nodiscard]] const QString& file() const { return file_; }

private:
    void renderFinished();
    void load(const QString& file);
    void mediaStatusChanged();
    void playbackStateChanged();
    void showFrameAt(qint64 ms);
    void finishScrub();

    RenderQueue* queue_;  // not owned
    std::shared_ptr<const RunData> run_;
    QMediaPlayer* player_;  // child
    QPointer<QVideoSink> sink_;
    QFutureWatcher<RenderResult>* watcher_;  // child
    FutureGuard<RenderResult> render_;
    QString file_;
    std::optional<double> pendingSeek_;
    bool playWhenReady_ = false;
    // AVFoundation shows no picture for a seek while paused, so the engine plays until the
    // first frame arrives and pauses again; the state stays Paused meanwhile.
    bool scrubbing_ = false;
    qint64 scrubTarget_ = 0;
    QMetaObject::Connection scrubConnection_;
};

} // namespace abrviz
