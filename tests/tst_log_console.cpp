#include "test_paths.h"

#include "log_console.h"
#include "main_window.h"
#include "session.h"
#include "time_cursor.h"

#include "abrviz/core/load/bundle_reader.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>

#include <memory>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 30000;

QString bundle(const char* name) { return test::dataPath(QString::fromLatin1(name)); }

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// The BBB logs, with `sim.log` repeated `copies` times so that filtering takes a while.
LogIndexPtr largeIndex(int copies)
{
    const QByteArray sim = readAll(simLogPath(bundle("bbb_36")));
    QByteArray big;
    big.reserve(sim.size() * copies);
    for (int i = 0; i < copies; ++i)
        big += sim;
    return LogIndex::fromText(big, readAll(policyLogPath(bundle("bbb_36"))), 7);
}

bool waitFor(QSignalSpy& spy)
{
    return !spy.isEmpty() || spy.wait(kTimeoutMs);
}

/// A session with a run open and its log index set, and a console on it.
struct Fixture {
    Session session;
    std::unique_ptr<LogConsole> console;
    std::shared_ptr<const RunData> run;

    explicit Fixture(LogIndexPtr index = {})
    {
        run = loadBundleSync(bundle("bbb_36")).run;
        console = std::make_unique<LogConsole>(&session);
        console->resize(900, 400);
        console->show();
        session.setRun(run);
        if (!index)
            index = LogIndex::open(simLogPath(bundle("bbb_36")), policyLogPath(bundle("bbb_36")), 7);
        QSignalSpy applied(console.get(), &LogConsole::filterApplied);
        session.setLogIndex(index, run.get());
        QVERIFY(waitFor(applied));
    }
};

} // namespace

class TestLogConsole : public QObject {
    Q_OBJECT

private slots:
    void showsTheBundleUeByDefault()
    {
        Fixture f;
        QCOMPARE(f.console->ueBox()->currentData().toInt(), 7);
        QCOMPARE(f.console->spec().minLevel, LogLevel::Info);
        QCOMPARE(f.console->model()->rows(), filterLog(*f.session.logIndex(), f.console->spec()));
        for (const qint32 row : f.console->model()->rows()) {
            const LogLine& line = f.session.logIndex()->line(row);
            QVERIFY(line.ue == 7 || line.ue < 0);
            QVERIFY(line.kind != LogKind::Frame);
        }
    }

    void typingIsDebouncedAndAppliesTheLastFilter()
    {
        Fixture f;
        QSignalSpy applied(f.console.get(), &LogConsole::filterApplied);
        QTest::keyClicks(f.console->filterEdit(), QStringLiteral("guard"));
        QVERIFY(f.console->isFiltering());
        QVERIFY(waitFor(applied));
        QTest::qWait(250);
        QCOMPARE(applied.size(), 1);  // five key presses, one request
        QCOMPARE(f.console->model()->rows().size(), std::size_t(6));  // three overrides, in both logs
    }

    void typingNeverBlocksTheGui()
    {
        Fixture f(largeIndex(60));
        f.console->levelBox()->setCurrentIndex(2);  // all lines: the slowest filter
        QSignalSpy applied(f.console.get(), &LogConsole::filterApplied);
        qint64 slowest = 0;
        const QString typed = QStringLiteral("segmentid 3");
        for (const QChar c : typed) {
            QElapsedTimer timer;
            timer.start();
            QTest::keyClick(f.console->filterEdit(), c.toLatin1());
            QCoreApplication::processEvents();  // runs any filter request that is due
            slowest = std::max(slowest, timer.elapsed());
            QTest::qWait(c == QLatin1Char(' ') ? 200 : 20);  // a pause starts a request
        }
        QVERIFY(waitFor(applied));
        QTRY_VERIFY_WITH_TIMEOUT(!f.console->isFiltering(), kTimeoutMs);
        QVERIFY2(slowest < 100, qPrintable(QStringLiteral("a key press took %1 ms").arg(slowest)));
        // Only the result of the last request is shown.
        QCOMPARE(f.console->model()->rows(), filterLog(*f.session.logIndex(), f.console->spec()));
    }

    void olderResultsAreDiscarded()
    {
        Fixture f(largeIndex(20));
        f.console->levelBox()->setCurrentIndex(2);
        QSignalSpy applied(f.console.get(), &LogConsole::filterApplied);
        // Requests back to back, without waiting: each replaces the one before.
        for (int i = 0; i < 4; ++i)
            f.console->ueBox()->setCurrentIndex(i % f.console->ueBox()->count());
        QTRY_VERIFY_WITH_TIMEOUT(!f.console->isFiltering(), kTimeoutMs);
        QTest::qWait(100);
        QVERIFY(!applied.isEmpty());
        // Whatever arrived, the table matches the filter that is set now.
        QCOMPARE(f.console->model()->rows(), filterLog(*f.session.logIndex(), f.console->spec()));
        const quint64 last = applied.last().first().toULongLong();
        for (const auto& args : applied)
            QVERIFY(args.first().toULongLong() <= last);
    }

    void followsTheCursor()
    {
        Fixture f;
        for (const double t : {20.0, 56.9, 100.0}) {
            f.session.cursor()->setTime(t, TimeCursor::Source::Timeline);
            const int row = f.console->table()->currentIndex().row();
            QVERIFY(row >= 0);
            QVERIFY(f.console->model()->lineAt(row)->time <= t);
            if (row + 1 < f.console->model()->rowCount())
                QVERIFY(f.console->model()->lineAt(row + 1)->time > t);
        }
        // Following does not move the cursor itself.
        QCOMPARE(f.session.cursor()->time(), 100.0);
    }

    void selectingALineMovesTheCursor()
    {
        Fixture f;
        f.console->setFollowCursor(false);
        QSignalSpy moved(f.session.cursor(), &TimeCursor::timeChanged);
        const int row = f.console->model()->rowAtTime(40.0);
        QVERIFY(row > 0);
        const QModelIndex index = f.console->model()->index(row, LogModel::Text);
        f.console->table()->scrollTo(index);
        QTest::mouseClick(f.console->table()->viewport(), Qt::LeftButton, {},
                          f.console->table()->visualRect(index).center());
        QCOMPARE(moved.size(), 1);
        QCOMPARE(moved.first().at(1).value<TimeCursor::Source>(), TimeCursor::Source::Logs);
        QCOMPARE(f.session.cursor()->time(), f.console->model()->lineAt(row)->time);
    }

    void closingTheRunDuringFilteringIsClean()
    {
        // Run under ASan: the run and its index go away while filter tasks hold them.
        for (int i = 0; i < 6; ++i) {
            Fixture f(largeIndex(60));
            f.console->filterEdit()->setText(QStringLiteral("frame"));
            f.console->levelBox()->setCurrentIndex(2);  // requests at once, without the delay
            QVERIFY(f.console->isFiltering());
            if (i % 2 == 0) {
                f.session.clear();
                QCOMPARE(f.console->model()->rowCount(), 0);
            } else {
                f.console.reset();  // destroyed while its task runs
            }
        }
    }

    void windowBuildsTheIndexAfterLoading()
    {
        MainWindow window;
        QSignalSpy ready(&window, &MainWindow::logIndexReady);
        window.openBundle(bundle("chef_cooking_18"));
        QVERIFY(waitFor(ready));
        QVERIFY(window.session()->logIndex());
        QCOMPARE(window.session()->logIndex()->nodeId(), 6);
        QSignalSpy applied(window.logs(), &LogConsole::filterApplied);
        QVERIFY(waitFor(applied));
        QVERIFY(window.logs()->model()->rowCount() > 0);
    }

    void closingTheWindowWhileIndexingIsClean()
    {
        for (int i = 0; i < 5; ++i) {
            auto window = std::make_unique<MainWindow>();
            QSignalSpy opened(window.get(), &MainWindow::bundleOpened);
            window->openBundle(bundle(i % 2 ? "bbb_36" : "chef_cooking_18"));
            QVERIFY(waitFor(opened));  // indexing starts now
            window.reset();
        }
    }
};

QTEST_MAIN(TestLogConsole)
#include "tst_log_console.moc"
