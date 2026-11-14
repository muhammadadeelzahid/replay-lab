#include "synthetic_run.h"
#include "test_paths.h"

#include "main_window.h"
#include "replay_player.h"
#include "session.h"
#include "time_cursor.h"

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/render/render_queue.h"
#include "abrviz/core/util/process_guard.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 30000;

/// The picture in `sink`, small and grey, for comparing engines.
QImage thumbnail(QVideoSink* sink)
{
    return sink->videoFrame().toImage().scaled(96, 54, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

/// Mean absolute difference of two thumbnails, 0..255.
double difference(const QImage& a, const QImage& b)
{
    if (a.size() != b.size() || a.isNull())
        return 255.0;
    double total = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* ra = a.constScanLine(y);
        const uchar* rb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x)
            total += std::abs(int(ra[x]) - int(rb[x]));
    }
    return total / (a.width() * a.height());
}

} // namespace

class TestReplayPlayer : public QObject {
    Q_OBJECT

private:
    QTemporaryDir cacheDir_;
    QString ffmpeg_;

    /// A session showing the synthetic run, with a player whose engine has rendered it.
    struct Fixture {
        Session session;
        RenderQueue queue;
        ReplayPlayer player;

        Fixture(const QString& cache, const QString& ffmpeg)
            : queue(RenderQueue::Options{cache, ffmpeg, 0})
            , player(&session, &queue)
        {
            player.resize(960, 600);
            // On macOS these tests use the Cocoa platform; keep the window off the screen.
            player.setAttribute(Qt::WA_DontShowOnScreen);
            player.show();
            player.setEngine(Engine::Prerendered);
            session.setRun(test::syntheticRun(1.0));
        }

        ~Fixture() { player.releaseEngine(); }  // before the queue it uses

        bool render()
        {
            player.prepare();
            QElapsedTimer timer;
            timer.start();
            while (player.engine()->state() != ReplaySource::State::Paused
                   && timer.elapsed() < kTimeoutMs)
                QTest::qWait(20);
            return player.engine()->state() == ReplaySource::State::Paused;
        }
    };

private slots:
    void initTestCase()
    {
        ffmpeg_ = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg_.isEmpty())
            QSKIP("ffmpeg not found");
    }

    void asksForARenderFirst()
    {
        QTemporaryDir empty;
        Fixture f(empty.path(), ffmpeg_);
        QVERIFY(!f.player.engine()->isReady());
        QVERIFY(f.player.messageText().contains(QStringLiteral("Render Video")));
        QVERIFY(f.render());
        QVERIFY(f.player.messageText().isEmpty());
        QVERIFY(f.player.overlayText().contains(QStringLiteral("Segment")));
        QVERIFY(f.player.overlayText().contains(QStringLiteral("Pre-rendered")));
    }

    void cursorMovesTheReplay()
    {
        Fixture f(cacheDir_.path(), ffmpeg_);
        QVERIFY(f.render());
        const PlaybackClock& clock = f.session.clock();
        // Inside segment 0, segment 1, the stall, and segment 2.
        for (const double sim : {2.0, 5.5, 8.0, 9.5}) {
            f.session.cursor()->setTime(sim, TimeCursor::Source::Timeline);
            const double expected = clock.simToReplay(sim);
            QTRY_VERIFY2(std::abs(f.player.engine()->position() - expected) < 0.05,
                         qPrintable(QStringLiteral("sim %1: replay %2, expected %3")
                                        .arg(sim)
                                        .arg(f.player.engine()->position())
                                        .arg(expected)));
        }
        QCOMPARE(f.session.cursor()->time(), 9.5);  // seeking never moved the cursor
    }

    void replayMovesTheCursor()
    {
        Fixture f(cacheDir_.path(), ffmpeg_);
        QVERIFY(f.render());
        f.session.cursor()->setTime(2.0, TimeCursor::Source::Timeline);
        QTRY_VERIFY(std::abs(f.player.engine()->position() - f.session.clock().simToReplay(2.0)) < 0.05);

        QSignalSpy moved(f.session.cursor(), &TimeCursor::timeChanged);
        f.player.play();
        QTRY_VERIFY_WITH_TIMEOUT(f.session.cursor()->time() > 2.6, kTimeoutMs);
        f.player.pause();
        QTRY_COMPARE(f.player.engine()->state(), ReplaySource::State::Paused);

        QVERIFY(!moved.isEmpty());
        for (const auto& args : moved)
            QCOMPARE(args.at(1).value<TimeCursor::Source>(), TimeCursor::Source::Player);
        // The cursor is where the replay is, in simulation time.
        const double expected = f.session.clock().replayToSim(f.player.engine()->position());
        QVERIFY2(std::abs(f.session.cursor()->time() - expected) < 0.2,
                 qPrintable(QStringLiteral("%1 vs %2").arg(f.session.cursor()->time()).arg(expected)));
    }

    void overlayShowsTheMoment()
    {
        Fixture f(cacheDir_.path(), ffmpeg_);
        f.session.cursor()->setTime(0.6, TimeCursor::Source::Timeline);
        QVERIFY(f.player.overlayText().startsWith(QStringLiteral("Before playback starts")));
        f.session.cursor()->setTime(5.0, TimeCursor::Source::Timeline);
        QVERIFY2(f.player.overlayText().contains(QStringLiteral("Segment 1")), qPrintable(f.player.overlayText()));
        QVERIFY(f.player.overlayText().contains(QStringLiteral("320 x 180")));
        QVERIFY(f.player.overlayText().contains(QStringLiteral("Buffer 2.00 s")));
        f.session.cursor()->setTime(8.0, TimeCursor::Source::Timeline);
        QVERIFY2(f.player.overlayText().contains(QStringLiteral("STALLED")), qPrintable(f.player.overlayText()));
        f.session.cursor()->setTime(f.session.endTime(), TimeCursor::Source::Timeline);
        QVERIFY2(f.player.overlayText().contains(QStringLiteral("cut off")), qPrintable(f.player.overlayText()));
    }

    void bothEnginesShowTheSamePicture()
    {
        if (!ReplaySourceFactory::isAvailable(Engine::GStreamer))
            QSKIP("built without GStreamer");
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        const LoadResult loaded = loadBundleSync(
            QDir(test::bundlesPath()).filePath(QStringLiteral("peace_autumn_lake_llm_9Mbps_run_4")));
        QVERIFY(loaded.run);

        Session session;
        RenderQueue queue(RenderQueue::Options{cacheDir_.path(), ffmpeg_, 0});
        ReplayPlayer player(&session, &queue);
        player.resize(960, 600);
        player.setAttribute(Qt::WA_DontShowOnScreen);
        player.show();
        session.setRun(loaded.run);

        // The middle of segments at different rungs, and the middle of the stall.
        std::vector<double> times;
        int lastRung = -2;
        for (const auto& piece : session.clock().pieces()) {
            const double middle = (piece.simStart + piece.simEnd) / 2.0;
            if (piece.kind == PlaybackClock::Piece::Kind::Stall) {
                times.push_back(middle);
                continue;
            }
            const int rung = loaded.run->segments[static_cast<std::size_t>(piece.segment)].playedRung;
            if (rung != lastRung && times.size() < 7)
                times.push_back(middle);
            lastRung = rung;
        }
        QVERIFY(std::ranges::any_of(session.clock().pieces(), [](const auto& p) {
            return p.kind == PlaybackClock::Piece::Kind::Stall;
        }));

        std::vector<QImage> pictures[2];
        for (const Engine engine : {Engine::GStreamer, Engine::Prerendered}) {
            QVERIFY(player.setEngine(engine));
            player.prepare();
            QTRY_VERIFY_WITH_TIMEOUT(player.engine()->state() == ReplaySource::State::Paused, 120000);
            for (const double time : times) {
                session.cursor()->setTime(time, TimeCursor::Source::Timeline);
                const double replay = session.clock().simToReplay(time);
                QTRY_VERIFY(std::abs(player.engine()->position() - replay) < 0.05);
                QTest::qWait(800);  // the frame at the new position (and the scrub's pause)
                pictures[engine == Engine::GStreamer ? 0 : 1].push_back(thumbnail(player.videoSink()));
            }
        }

        // Each position looks the same in both engines, and unlike the other positions.
        for (std::size_t i = 0; i < times.size(); ++i) {
            const double same = difference(pictures[0][i], pictures[1][i]);
            double others = 255.0;
            for (std::size_t j = 0; j < times.size(); ++j) {
                if (j != i)
                    others = std::min(others, difference(pictures[0][i], pictures[1][j]));
            }
            QVERIFY2(same < 6.0 && same < others / 2.0,
                     qPrintable(QStringLiteral("sim %1: same %2, closest other %3")
                                    .arg(times[i]).arg(same).arg(others)));
        }
    }

    void closingTheWindowWhileRenderingIsClean()
    {
        // A slow stand-in for ffmpeg, so the render is still running when the window closes.
        QTemporaryDir dir;
        const QString script = dir.filePath(QStringLiteral("slow-ffmpeg"));
        {
            QFile file(script);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#!/bin/sh\nsleep 30\n");
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        qputenv("ABRVIZ_FFMPEG", script.toUtf8());
        qputenv("ABRVIZ_RENDER_CACHE", dir.filePath(QStringLiteral("cache")).toUtf8());
        auto window = std::make_unique<MainWindow>();
        qunsetenv("ABRVIZ_FFMPEG");
        QSignalSpy opened(window.get(), &MainWindow::bundleOpened);
        window->openBundle(test::dataPath(QStringLiteral("chef_cooking_18")));
        QVERIFY(!opened.isEmpty() || opened.wait(kTimeoutMs));
        QVERIFY(window->setEngine(Engine::Prerendered));
        window->player()->prepare();
        QTRY_VERIFY_WITH_TIMEOUT(ProcessGuard::liveCount() > 0, kTimeoutMs);

        QElapsedTimer timer;
        timer.start();
        window.reset();
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(ProcessGuard::liveCount(), 0);
    }
};

QTEST_MAIN(TestReplayPlayer)
#include "tst_replay_player.moc"
