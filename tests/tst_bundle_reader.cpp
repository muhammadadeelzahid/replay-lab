#include "abrviz/core/load/bundle_reader.h"
#include "test_paths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace abrviz;

namespace {

/// Copies a test bundle so a test can break it.
QString copyBundle(const QString& from, const QTemporaryDir& to)
{
    const QDir source(from);
    const QString target = to.filePath(source.dirName());
    QDir().mkpath(target);
    const auto copyTree = [&](const auto& self, const QString& relative) -> void {
        const QDir dir(source.filePath(relative));
        for (const QString& name : dir.entryList(QDir::Files)) {
            QFile::copy(dir.filePath(name), QDir(target).filePath(relative + '/' + name));
        }
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir(target).mkpath(relative + '/' + sub);
            self(self, relative + '/' + sub);
        }
    };
    copyTree(copyTree, QStringLiteral("."));
    return target;
}

int countWarnings(const RunData& run, Warning::Kind kind)
{
    return static_cast<int>(
        std::ranges::count_if(run.warnings, [kind](const Warning& w) { return w.kind == kind; }));
}

} // namespace

class TestBundleReader : public QObject {
    Q_OBJECT

private slots:
    void chefCooking()
    {
        const LoadResult result = loadBundleSync(test::dataPath(QStringLiteral("chef_cooking_18")));
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QVERIFY(result.run);
        const RunData& run = *result.run;

        QCOMPARE(run.info.nodeId, 6);
        QCOMPARE(run.segments.size(), 11u);
        QCOMPARE(run.decisions.size(), 11u);
        QVERIFY(!run.decisions.back().used);

        // Segment 0 is the fixed 250 kbps request; later segments use the previous record.
        QCOMPARE(run.segments[0].playedRung, 0);
        QCOMPARE(run.segments[1].playedRung, run.decisions[0].chosenRung);

        // The frame lines agree with the corrected rungs, and the video played to the end.
        QCOMPARE(run.playback.size(), 11u);
        QCOMPARE(countWarnings(run, Warning::Kind::RungMismatch), 0);
        QVERIFY(!run.playbackCutOff());
        QVERIFY(run.buffer.size() > 1000);

        // The test data has no media files.
        QCOMPARE(countWarnings(run, Warning::Kind::MissingMedia), 1);
        QCOMPARE(run.segments[0].mediaDuration, 2.0);

        // policy.log guard reasons line up with the exporter's flag.
        for (const Decision& d : run.decisions) {
            if (d.llmRung)
                QVERIFY(d.guardOverride);
        }
    }

    void bigBuckBunnyWithStall()
    {
        const LoadResult result = loadBundleSync(test::dataPath(QStringLiteral("bbb_36")));
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        const RunData& run = *result.run;

        QCOMPARE(run.stalls.size(), 1u);
        QVERIFY(qAbs(run.stalls[0].start - 56.8597) < 1e-6);
        QCOMPARE(countWarnings(run, Warning::Kind::StallMismatch), 0);
        QCOMPARE(countWarnings(run, Warning::Kind::LogAgreement), 1);

        QVERIFY(run.playbackCutOff());
        QCOMPARE(run.playback.back().segment, 54);
        QCOMPARE(run.playback.back().framesPlayed, 39);
        QCOMPARE(countWarnings(run, Warning::Kind::PlaybackCutOff), 1);
    }

    void missingRunJsonFails()
    {
        QTemporaryDir dir;
        const QString bundle = copyBundle(test::dataPath(QStringLiteral("chef_cooking_18")), dir);
        QVERIFY(QFile::remove(QDir(bundle).filePath(QStringLiteral("run.json"))));
        const LoadResult result = loadBundleSync(bundle);
        QVERIFY(!result.run);
        QVERIFY(result.errors.first().startsWith(QStringLiteral("run.json")));
    }

    void brokenLineIsReportedWithItsNumber()
    {
        QTemporaryDir dir;
        const QString bundle = copyBundle(test::dataPath(QStringLiteral("chef_cooking_18")), dir);
        QFile samples(QDir(bundle).filePath(QStringLiteral("samples.jsonl")));
        QVERIFY(samples.open(QIODevice::Append));
        samples.write("{\"t\": \n");
        samples.close();

        const LoadResult result = loadBundleSync(bundle);
        QVERIFY(result.run);  // samples are not essential
        QCOMPARE(result.errors.size(), 1);
        QVERIFY2(result.errors.first().startsWith(QStringLiteral("samples.jsonl:12:")),
                 qPrintable(result.errors.first()));
    }

    void missingSimLogIsAnErrorButTheRunLoads()
    {
        QTemporaryDir dir;
        const QString bundle = copyBundle(test::dataPath(QStringLiteral("chef_cooking_18")), dir);
        QVERIFY(QFile::remove(QDir(bundle).filePath(QStringLiteral("logs/sim.log"))));
        const LoadResult result = loadBundleSync(bundle);
        QVERIFY(result.run);
        QVERIFY(result.run->playback.empty());
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(result.errors.first().startsWith(QStringLiteral("logs/sim.log")));
    }

    void allDeliveredBundles()
    {
        if (!test::haveFullBundles())
            QSKIP("viewer_bundles/ is not present");
        const QDir root(test::bundlesPath());
        const auto names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(names.size(), 14);
        for (const QString& name : names) {
            const LoadResult result = loadBundleSync(root.filePath(name));
            QVERIFY2(result.ok(), qPrintable(name + ": " + result.errors.join('\n')));
            const RunData& run = *result.run;
            QVERIFY2(!run.hasWarning(Warning::Kind::RungMismatch), qPrintable(name));
            QVERIFY2(!run.hasWarning(Warning::Kind::StallMismatch), qPrintable(name));
            QVERIFY2(!run.hasWarning(Warning::Kind::MissingMedia), qPrintable(name));
            QCOMPARE(run.playbackCutOff(), name.startsWith(QStringLiteral("big_buck_bunny")));
        }

        // Media durations come from the files: seg_0000 of BBB is 4 s, the last one is short.
        const auto bbb = loadBundleSync(
            root.filePath(QStringLiteral("big_buck_bunny_llm_18Mbps_run_3")));
        QCOMPARE(bbb.run->segments.front().mediaDuration, 4.0);
        QCOMPARE(bbb.run->segments[1].mediaDuration, 2.0);

        const auto skipped = loadBundleSync(
            root.filePath(QStringLiteral("pexels_34797065_llm_36Mbps_run_2")));
        QVERIFY(skipped.run->hasWarning(Warning::Kind::SkippedSegments));
        QVERIFY(!skipped.run->segments[13].played);
        QVERIFY(skipped.run->segments[14].played);
    }
};

QTEST_GUILESS_MAIN(TestBundleReader)
#include "tst_bundle_reader.moc"
