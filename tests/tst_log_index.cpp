#include "test_paths.h"

#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/logs/log_filter.h"
#include "abrviz/core/logs/log_index.h"

#include <QFile>
#include <QTest>
#include <QThreadPool>

#include <algorithm>
#include <random>
#include <thread>

using namespace abrviz;

namespace {

QString bundle(const char* name) { return test::dataPath(QString::fromLatin1(name)); }

LogIndexPtr openIndex(const QString& directory, int nodeId)
{
    return LogIndex::open(simLogPath(directory), policyLogPath(directory), nodeId);
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int countKind(const LogIndex& index, LogKind kind, int ue = LogFilterSpec::kAllUes,
              std::optional<LogSource> source = std::nullopt)
{
    return static_cast<int>(std::ranges::count_if(index.lines(), [&](const LogLine& l) {
        return l.kind == kind && (ue == LogFilterSpec::kAllUes || l.ue == ue)
            && (!source || l.source == *source);
    }));
}

/// A large index for timing tests: the BBB log repeated `copies` times.
LogIndexPtr largeIndex(int copies)
{
    const QByteArray sim = readAll(simLogPath(bundle("bbb_36")));
    const QByteArray policy = readAll(policyLogPath(bundle("bbb_36")));
    QByteArray bigSim;
    bigSim.reserve(sim.size() * copies);
    for (int i = 0; i < copies; ++i)
        bigSim += sim;
    return LogIndex::fromText(bigSim, policy, 7);
}

} // namespace

class TestLogIndex : public QObject {
    Q_OBJECT

private slots:
    void indexesBothLogs()
    {
        const LogIndexPtr index = openIndex(bundle("bbb_36"), 7);
        QVERIFY2(index->errors().isEmpty(), qPrintable(index->errors().join(u'\n')));
        QCOMPARE(index->ues(), (QList<int>{5, 6, 7}));

        const LoadResult loaded = loadBundleSync(bundle("bbb_36"));
        QVERIFY(loaded.run);
        // One frame line per buffer point of the bundle's UE, one stall, three overrides.
        QCOMPARE(countKind(*index, LogKind::Frame, 7), static_cast<int>(loaded.run->buffer.size()));
        QCOMPARE(countKind(*index, LogKind::Pause, 7), 1);
        QCOMPARE(countKind(*index, LogKind::Resume, 7), 1);
        constexpr auto policy = LogSource::Policy;
        constexpr auto sim = LogSource::Sim;
        QCOMPARE(countKind(*index, LogKind::GuardOverride, 7, policy), 3);
        QCOMPARE(countKind(*index, LogKind::Policy, 7, policy)
                     + countKind(*index, LogKind::GuardOverride, 7, policy),
                 59);
        // `sim.log` echoes the policy's decisions as `[Python][LLM] node=7 ...` lines.
        QCOMPARE(countKind(*index, LogKind::GuardOverride, 7, sim), 3);
        QCOMPARE(countKind(*index, LogKind::Policy, 7, sim)
                     + countKind(*index, LogKind::GuardOverride, 7, sim),
                 59);
        QVERIFY(countKind(*index, LogKind::Setup) > 10);
        QVERIFY(countKind(*index, LogKind::Progress) > 100);
    }

    void linesAreOrderedByTime()
    {
        const LogIndexPtr index = openIndex(bundle("bbb_36"), 7);
        QVERIFY(std::ranges::is_sorted(index->lines(), {}, &LogLine::time));
        // Setup lines come first, at time 0.
        QCOMPARE(index->line(0).kind, LogKind::Setup);
        QCOMPARE(index->line(0).time, 0.0);
        const qsizetype row = index->firstRowAt(56.86);
        QVERIFY(row < index->size());
        QVERIFY(index->line(row).time >= 56.86);
        QVERIFY(row == 0 || index->line(row - 1).time < 56.86);
    }

    void textRefersIntoTheFiles()
    {
        const LogIndexPtr index = openIndex(bundle("chef_cooking_18"), 6);
        const auto it = std::ranges::find(index->lines(), LogKind::GuardOverride, &LogLine::kind);
        QVERIFY(it != index->lines().end());
        const QString text = index->displayText(*it);
        QVERIFY(text.contains(QStringLiteral("+guard(")));
        QCOMPARE(QString::fromUtf8(index->lowerText(*it).toByteArray()), text.toLower());
        QCOMPARE(it->source, LogSource::Policy);
    }

    void missingFilesAreReported()
    {
        const LogIndexPtr index = LogIndex::open(QStringLiteral("/nonexistent/sim.log"),
                                                 QStringLiteral("/nonexistent/policy.log"), 7);
        QCOMPARE(index->size(), 0);
        QCOMPARE(index->errors().size(), 2);
    }

    void filterWordsPhrasesAndExclusions()
    {
        const LogIndexPtr index = openIndex(bundle("bbb_36"), 7);
        LogFilterSpec spec;
        spec.minLevel = LogLevel::Debug;
        const auto all = filterLog(*index, spec);
        QCOMPARE(static_cast<qsizetype>(all.size()), index->size());

        spec.text = QStringLiteral("guard");
        const auto guard = filterLog(*index, spec);
        for (const qint32 row : guard)
            QVERIFY(index->lowerText(index->line(row)).contains("guard"));
        QVERIFY(!guard.empty());

        spec.text = QStringLiteral("\"play resumed\"");
        QCOMPARE(filterLog(*index, spec).size(), std::size_t(countKind(*index, LogKind::Resume)));

        spec.text = QStringLiteral("PLAYING -FRAME");  // upper case: exact case
        QVERIFY(filterLog(*index, spec).empty());
        spec.text = QStringLiteral("Playing");  // has upper case, so no match for PLAYING
        QVERIFY(filterLog(*index, spec).empty());
        spec.text = QStringLiteral("playing");
        QCOMPARE(filterLog(*index, spec).size(), std::size_t(countKind(*index, LogKind::Frame)));
    }

    void filterByUeAndLevel()
    {
        const LogIndexPtr index = openIndex(bundle("bbb_36"), 7);
        LogFilterSpec spec;
        spec.ue = 7;
        spec.minLevel = LogLevel::Warning;
        const auto rows = filterLog(*index, spec);
        // One stall and the three guard overrides, each in both logs.
        QCOMPARE(rows.size(), std::size_t(1 + 3 + 3));
        for (const qint32 row : rows) {
            QCOMPARE(index->line(row).ue, qint16(7));
            QCOMPARE(index->line(row).level(), LogLevel::Warning);
        }
        spec.minLevel = LogLevel::Info;  // the default hides frame and progress lines
        for (const qint32 row : filterLog(*index, spec))
            QVERIFY(index->line(row).kind != LogKind::Frame);
    }

    void olderRequestsAreNotCurrent()
    {
        const LogIndexPtr index = openIndex(bundle("chef_cooking_18"), 6);
        LogFilter filter;
        auto first = filter.request(index, {});
        auto second = filter.request(index, {QStringLiteral("decision"), 6, LogLevel::Info});
        second.waitForFinished();
        first.waitForFinished();
        QCOMPARE(second.resultCount(), 1);
        QVERIFY(filter.isCurrent(second.result().generation));
        if (first.resultCount() == 1)  // it may also have stopped early
            QVERIFY(!filter.isCurrent(first.result().generation));
        QCOMPARE(second.result().rows,
                 filterLog(*index, {QStringLiteral("decision"), 6, LogLevel::Info}));

        filter.invalidate();
        QVERIFY(!filter.isCurrent(second.result().generation));
    }

    void newerRequestStopsOlderTask()
    {
        const LogIndexPtr index = largeIndex(60);  // about 400 000 lines
        QThreadPool pool;
        pool.setMaxThreadCount(2);
        LogFilter filter;
        LogFilterSpec slow;
        slow.text = QStringLiteral("no-such-text");
        slow.minLevel = LogLevel::Debug;
        auto first = filter.request(index, slow, &pool);
        auto second = filter.request(index, slow, &pool);
        second.waitForFinished();
        first.waitForFinished();
        QCOMPARE(first.resultCount(), 0);  // out of date, so it gave no result
        QCOMPARE(second.resultCount(), 1);
    }

    void cancelledRequestGivesNoResult()
    {
        const LogIndexPtr index = largeIndex(60);
        LogFilter filter;
        auto future = filter.request(index, {QString(), LogFilterSpec::kAllUes, LogLevel::Debug});
        future.cancel();
        future.waitForFinished();
        QCOMPARE(future.resultCount(), 0);
    }

    void tasksOutliveFilterAndIndexOwner()
    {
        // Requests at random moments, the filter and the caller's index reference dropped
        // while tasks run: each task keeps what it needs. Checked under ASan and TSan.
        std::mt19937 random(7);
        QThreadPool pool;
        pool.setMaxThreadCount(4);
        QList<QFuture<LogFilterResult>> futures;
        for (int round = 0; round < 20; ++round) {
            auto filter = std::make_unique<LogFilter>();
            LogIndexPtr index = largeIndex(4);
            for (int i = 0; i < 5; ++i) {
                LogFilterSpec spec;
                spec.text = QString::number(random() % 100);
                spec.minLevel = LogLevel::Debug;
                futures.append(filter->request(index, spec, &pool));
            }
            index.reset();
            filter.reset();
            if (round % 3 == 0)
                std::this_thread::sleep_for(std::chrono::microseconds(random() % 2000));
        }
        for (auto& future : futures)
            future.waitForFinished();
        pool.waitForDone();
    }

    void fullBundlesIndex()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ not present");
        const QStringList bundles = QDir(test::bundlesPath()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(bundles.size(), 14);
        for (const QString& name : bundles) {
            const QString directory = QDir(test::bundlesPath()).filePath(name);
            const LoadResult loaded = loadBundleSync(directory);
            QVERIFY(loaded.run);
            const int node = loaded.run->info.nodeId;
            const LogIndexPtr index = openIndex(directory, node);
            QVERIFY2(index->errors().isEmpty(), qPrintable(name));
            QCOMPARE(countKind(*index, LogKind::Frame, node),
                     static_cast<int>(loaded.run->buffer.size()));
            QVERIFY(std::ranges::is_sorted(index->lines(), {}, &LogLine::time));
        }
    }
};

QTEST_GUILESS_MAIN(TestLogIndex)
#include "tst_log_index.moc"
