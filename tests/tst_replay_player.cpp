#include "synthetic_run.h"
#include "test_paths.h"

#include "main_window.h"
#include "replay_player.h"
#include "session.h"
#include "time_cursor.h"

#include "abrviz/core/render/render_queue.h"
#include "abrviz/core/util/process_guard.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 30000;

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
        QVERIFY(f.player.overlayText().contains(QStringLiteral("64 x 36")));
        QVERIFY(f.player.overlayText().contains(QStringLiteral("Buffer 2.00 s")));
        f.session.cursor()->setTime(8.0, TimeCursor::Source::Timeline);
        QVERIFY2(f.player.overlayText().contains(QStringLiteral("STALLED")), qPrintable(f.player.overlayText()));
        f.session.cursor()->setTime(f.session.endTime(), TimeCursor::Source::Timeline);
        QVERIFY2(f.player.overlayText().contains(QStringLiteral("cut off")), qPrintable(f.player.overlayText()));
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
