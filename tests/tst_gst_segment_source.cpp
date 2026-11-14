#include "synthetic_run.h"

#include "gst/gst_segment_source.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>
#include <memory>

using namespace abrviz;
using State = ReplaySource::State;

namespace {

constexpr int kTimeoutMs = 20000;

bool waitForState(const ReplaySource& source, State state, int timeoutMs = kTimeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (source.state() != state && timer.elapsed() < timeoutMs)
        QTest::qWait(10);
    return source.state() == state;
}

struct Observation {
    qint64 wallMs;
    double position;
    State state;
    double media;
};

} // namespace

class TestGstSegmentSource : public QObject {
    Q_OBJECT

private:
    /// A source with the synthetic run (stall `stall` s), prerolled and paused.
    std::unique_ptr<GstSegmentSource> ready(QVideoSink* sink, double stall = 1.0)
    {
        auto source = std::make_unique<GstSegmentSource>();
        source->setVideoSink(sink);
        source->setRun(test::syntheticRun(stall));
        if (!waitForState(*source, State::Paused))
            return nullptr;
        return source;
    }

private slots:
    void spansMergeSegmentsBetweenStalls()
    {
        const PlaybackClock clock(*test::syntheticRun(1.0));
        const auto spans = GstSegmentSource::makeSpans(clock);
        QCOMPARE(spans.size(), std::size_t{3});
        QVERIFY(!spans[0].stall);
        QCOMPARE(spans[0].mediaEnd, 5.0);
        QVERIFY(spans[1].stall);
        QCOMPARE(spans[1].replayStart, 5.0);
        QCOMPARE(spans[1].replayEnd, 6.0);
        QCOMPARE(spans[1].mediaStart, 5.0);
        QVERIFY(!spans[2].stall);
        QCOMPARE(spans[2].mediaStart, 5.0);
        QCOMPARE(spans[2].mediaEnd, 6.25);  // cut off half way through the third segment
        QCOMPARE(spans[2].replayEnd, 7.25);
    }

    void prerollShowsAFullHdFrame()
    {
        QVideoSink sink;
        QSignalSpy frames(&sink, &QVideoSink::videoFrameChanged);
        auto source = ready(&sink);
        QVERIFY(source);
        QTRY_VERIFY(!frames.isEmpty());
        const QVideoFrame frame = sink.videoFrame();
        QCOMPARE(frame.size(), QSize(1920, 1080));
        QCOMPARE(frame.pixelFormat(), QVideoFrameFormat::Format_NV12);
        QVERIFY(!frame.toImage().isNull());  // maps through GstVideoBuffer
    }

    void seeksShowTheRightMoment()
    {
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        // Inside the first span, inside the stall (the last frame before it), after the stall.
        for (const auto& [replay, media] : {std::pair{1.2, 1.2}, {4.4, 4.4}, {5.5, 5.0}, {6.5, 5.5}}) {
            const qint64 before = source->framesShown();
            source->seek(replay);
            QCOMPARE(source->position(), replay);
            QTRY_VERIFY(source->framesShown() > before);
            QVERIFY2(std::abs(source->frameMediaTime() - media) < 0.05,
                     qPrintable(QStringLiteral("replay %1: media %2, expected %3")
                                    .arg(replay).arg(source->frameMediaTime()).arg(media)));
            QCOMPARE(source->state(), State::Paused);
        }
    }

    void playsThroughTheStallAndStopsAtTheCutOff()
    {
        QVideoSink sink;
        auto source = ready(&sink);
        QVERIFY(source);
        source->seek(3.5);
        QTRY_COMPARE(source->position(), 3.5);

        std::vector<Observation> samples;
        QElapsedTimer wall;
        wall.start();
        const auto record = [&] {
            samples.push_back({wall.elapsed(), source->position(), source->state(), source->frameMediaTime()});
        };
        QObject::connect(source.get(), &ReplaySource::positionChanged, source.get(), record);
        source->play();
        QVERIFY(waitForState(*source, State::Ended));
        const qint64 elapsed = wall.elapsed();

        // 3.75 s of replay from 3.5 to the end (7.25), in real time.
        QVERIFY2(std::abs(static_cast<double>(elapsed) / 1000.0 - 3.75) < 0.5, qPrintable(QString::number(elapsed)));
        QCOMPARE(source->position(), 7.25);

        // Positions only move forward, and the stall holds the frame for its second.
        double last = 3.5;
        qint64 stallStart = -1;
        qint64 stallEnd = -1;
        for (const Observation& s : samples) {
            QVERIFY2(s.position >= last - 1e-6, qPrintable(QStringLiteral("%1 after %2").arg(s.position).arg(last)));
            last = s.position;
            if (s.state == State::Stalled) {
                if (stallStart < 0)
                    stallStart = s.wallMs;
                stallEnd = s.wallMs;
                QVERIFY2(std::abs(s.media - 5.0) < 0.05, qPrintable(QString::number(s.media)));
            }
        }
        QVERIFY(stallStart >= 0);
        QVERIFY2(stallEnd - stallStart > 800, qPrintable(QString::number(stallEnd - stallStart)));
        // The last frame shown is the cut-off point of the third segment.
        QVERIFY2(std::abs(source->frameMediaTime() - 6.25) < 0.1, qPrintable(QString::number(source->frameMediaTime())));
    }

    void pauseAndResumeInsideTheStall()
    {
        QVideoSink sink;
        auto source = ready(&sink, 2.0);
        QVERIFY(source);
        source->seek(5.5);  // half a second into the 2 s stall
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        QTest::qWait(500);
        source->pause();
        QCOMPARE(source->state(), State::Paused);
        const double paused = source->position();
        QVERIFY2(paused > 5.85 && paused < 6.2, qPrintable(QString::number(paused)));
        QTest::qWait(300);
        QCOMPARE(source->position(), paused);  // a paused stall does not run on
        source->play();
        QVERIFY(waitForState(*source, State::Playing, 3000));
        QVERIFY(source->position() >= 7.0 - 1e-6);
    }

    void seekingWhilePlayingKeepsPlaying()
    {
        QVideoSink sink;
        auto source = ready(&sink, 2.0);
        QVERIFY(source);
        source->seek(5.5);  // into the stall
        source->play();
        QCOMPARE(source->state(), State::Stalled);
        source->seek(2.0);  // out of the stall, into a segment
        QVERIFY(waitForState(*source, State::Playing, 2000));
        QTest::qWait(500);
        QVERIFY2(source->position() > 2.3 && source->position() < 2.8,
                 qPrintable(QString::number(source->position())));
        source->seek(6.0);  // back into the stall while playing
        QCOMPARE(source->state(), State::Stalled);
        QVERIFY(waitForState(*source, State::Playing, 2500));  // after the remaining 1.5 s
        QVERIFY(source->position() >= 7.0 - 1e-6);
        QVERIFY(waitForState(*source, State::Ended, 5000));
    }

    void deletingDuringPlaybackIsClean()
    {
        QVideoSink sink;
        for (int i = 0; i < 5; ++i) {
            auto source = ready(&sink, 0.3);
            QVERIFY(source);
            source->play();
            QTest::qWait(100 + 120 * i);  // playing, stalled, or past the stall
            source.reset();
        }
        // Deleting while the pipeline is still prerolling.
        for (int i = 0; i < 5; ++i) {
            auto source = std::make_unique<GstSegmentSource>();
            source->setVideoSink(&sink);
            source->setRun(test::syntheticRun());
            QTest::qWait(i * 5);
            source.reset();
        }
        // Replacing the run while playing.
        auto source = ready(&sink);
        QVERIFY(source);
        source->play();
        QTest::qWait(200);
        source->setRun(test::syntheticRun(0.5, QStringLiteral("other")));
        QVERIFY(waitForState(*source, State::Paused));
        QTest::qWait(100);  // no stale frame or bus message reaches the new run
        QCOMPARE(source->state(), State::Paused);
    }

    void missingMediaIsAnError()
    {
        auto run = std::make_shared<RunData>(*test::syntheticRun());
        run->segments[1].mediaPath = QStringLiteral("/no/such/file.mp4");
        GstSegmentSource source;
        QSignalSpy errors(&source, &ReplaySource::errorOccurred);
        source.setRun(run);
        QCOMPARE(source.state(), State::Error);
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("segment 1")));
        QVERIFY(!source.isReady());
    }
};

QTEST_MAIN(TestGstSegmentSource)
#include "tst_gst_segment_source.moc"
