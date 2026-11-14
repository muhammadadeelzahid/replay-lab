#include "background_mode.h"
#include "synthetic_run.h"
#include "test_paths.h"

#include "abrviz/core/playback/playback_clock.h"
#include "abrviz/core/render/render_queue.h"
#include "abrviz/media/prerendered_source.h"
#include "abrviz/media/replay_source_factory.h"

#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 30000;


struct TimedFrame {
    double position = 0.0;  ///< the engine's position when the frame arrived
    QImage image;
};

/// Plays `source` from `from` until its position passes `to`, keeping every frame shown.
/// (The AVFoundation backend shows no frame for a seek while paused, so frames are taken while
/// playing.)
std::vector<TimedFrame> playAndCollect(ReplaySource& source, QVideoSink& sink, double from,
                                       double to)
{
    std::vector<TimedFrame> frames;
    const auto connection = QObject::connect(&sink, &QVideoSink::videoFrameChanged, &sink,
                                             [&](const QVideoFrame& frame) {
                                                 frames.push_back({source.position(), frame.toImage()});
                                             });
    source.seek(from);
    source.play();
    QElapsedTimer timer;
    timer.start();
    while (source.position() < to && timer.elapsed() < kTimeoutMs)
        QTest::qWait(20);
    source.pause();
    QObject::disconnect(connection);
    return frames;
}

double difference(const QImage& a, const QImage& b)
{
    if (a.size() != b.size() || a.isNull())
        return 1e9;
    const QImage x = a.convertToFormat(QImage::Format_RGB32);
    const QImage y = b.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    for (int row = 0; row < x.height(); row += 4) {
        for (int col = 0; col < x.width(); col += 4) {
            const QRgb p = x.pixel(col, row);
            const QRgb q = y.pixel(col, row);
            sum += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q))
                + std::abs(qBlue(p) - qBlue(q));
        }
    }
    return sum / ((x.width() / 4) * (x.height() / 4) * 3.0);
}

} // namespace

class TestReplaySource : public QObject {
    Q_OBJECT

private:
    QTemporaryDir cacheDir_;
    std::unique_ptr<RenderQueue> queue_;

private slots:
    void initTestCase()
    {
        // These tests use the Cocoa platform; never take the focus from the app in use.
        abrviz::runInBackground();
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not found");
        queue_ = std::make_unique<RenderQueue>(RenderQueue::Options{cacheDir_.path(), ffmpeg, 0});
    }

    void cleanupTestCase() { queue_.reset(); }

    void factoryKnowsTheEngines()
    {
        QVERIFY(ReplaySourceFactory::isAvailable(Engine::Prerendered));
        QCOMPARE(engineFromKey(QStringLiteral("prerendered")), Engine::Prerendered);
        QCOMPARE(engineFromKey(QStringLiteral("GStreamer")), Engine::GStreamer);
        QVERIFY(!engineFromKey(QStringLiteral("vlc")));
        auto source = ReplaySourceFactory::create(Engine::Prerendered, queue_.get());
        QVERIFY(source);
        QVERIFY(source->parent() == nullptr);
    }

    void needsARenderFirst()
    {
        PrerenderedSource source(queue_.get());
        source.setRun(test::syntheticRun(1.0));
        QVERIFY(!source.isReady());
        QCOMPARE(source.state(), ReplaySource::State::Idle);

        QSignalSpy states(&source, &ReplaySource::stateChanged);
        QSignalSpy progress(&source, &ReplaySource::preparationProgress);
        source.prepare();
        QCOMPARE(source.state(), ReplaySource::State::Preparing);
        QTRY_VERIFY_WITH_TIMEOUT(source.state() == ReplaySource::State::Paused, kTimeoutMs);
        QVERIFY(source.isReady());
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().first().toDouble(), 1.0);

        // A second source for the same run finds the file in the cache at once.
        PrerenderedSource again(queue_.get());
        again.setRun(test::syntheticRun(1.0));
        QVERIFY(again.isReady());
        QCOMPARE(again.file(), source.file());
    }

    void playsAndSeeks()
    {
        PrerenderedSource source(queue_.get());
        QVideoSink sink;
        source.setVideoSink(&sink);
        source.setRun(test::syntheticRun(1.0));
        source.prepare();
        QTRY_VERIFY_WITH_TIMEOUT(source.state() == ReplaySource::State::Paused, kTimeoutMs);

        source.seek(1.0);
        QTRY_VERIFY(std::abs(source.position() - 1.0) < 0.05);
        const auto frames = playAndCollect(source, sink, 1.0, 1.6);
        QTRY_COMPARE(source.state(), ReplaySource::State::Paused);
        QVERIFY(!frames.empty());
        QCOMPARE(frames.back().image.size(), QSize(1920, 1080));  // every rung at 1080p
        QVERIFY2(source.position() >= 1.6, qPrintable(QString::number(source.position())));
        for (std::size_t i = 1; i < frames.size(); ++i)
            QVERIFY(frames[i].position >= frames[i - 1].position);

        // Playing to the end stops at the cut-off point.
        QSignalSpy states(&source, &ReplaySource::stateChanged);
        source.seek(6.9);
        source.play();
        QTRY_VERIFY_WITH_TIMEOUT(source.state() == ReplaySource::State::Ended, kTimeoutMs);
        QVERIFY(std::abs(source.position() - 7.25) < 0.1);
    }

    void stallShowsAsAFreeze()
    {
        const auto run = test::syntheticRun(1.0);
        const PlaybackClock clock(*run);
        // Replay time: segment 0 [0, 2.5), segment 1 [2.5, 5.0), stall [5.0, 6.0), segment 2.
        const auto stall = std::ranges::find(clock.pieces(), PlaybackClock::Piece::Kind::Stall,
                                             &PlaybackClock::Piece::kind);
        QVERIFY(stall != clock.pieces().end());
        QCOMPARE(stall->replayStart, 5.0);
        QCOMPARE(stall->replayEnd, 6.0);

        PrerenderedSource source(queue_.get());
        QVideoSink sink;
        source.setVideoSink(&sink);
        source.setRun(run);
        source.prepare();
        QTRY_VERIFY_WITH_TIMEOUT(source.state() == ReplaySource::State::Paused, kTimeoutMs);

        const auto frames = playAndCollect(source, sink, 4.0, 6.1);
        // Change from one frame to the next. The encoder keeps refining a repeated picture, so
        // frozen frames are not bit-identical, but they change far less than moving video.
        std::vector<double> inStall;
        std::vector<double> inSegment;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const double change = difference(frames[i].image, frames[i - 1].image);
            const double position = frames[i].position;
            if (position > 5.15 && position < 5.8)
                inStall.push_back(change);
            else if (position > 4.0 && position < 4.85)
                inSegment.push_back(change);
        }
        QVERIFY2(inStall.size() >= 3, qPrintable(QString::number(inStall.size())));
        QVERIFY(inSegment.size() >= 3);
        const double stallMax = *std::ranges::max_element(inStall);
        double segmentMean = 0.0;
        for (const double change : inSegment)
            segmentMean += change / static_cast<double>(inSegment.size());
        QVERIFY2(stallMax < 0.1, qPrintable(QString::number(stallMax)));
        QVERIFY2(segmentMean > 0.3, qPrintable(QString::number(segmentMean)));
    }

    void changingTheRunCancelsTheRender()
    {
        PrerenderedSource source(queue_.get());
        auto run = std::make_shared<RunData>(*test::syntheticRun(1.0));
        run->video.frameRate = 25.0;  // a different file
        source.setRun(run);
        source.prepare();
        QCOMPARE(source.state(), ReplaySource::State::Preparing);
        source.setRun(nullptr);  // cancels and waits
        QCOMPARE(source.state(), ReplaySource::State::Idle);
        QVERIFY(!source.isReady());
    }
};

QTEST_MAIN(TestReplaySource)
#include "tst_replay_source.moc"
