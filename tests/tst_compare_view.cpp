// CompareView: the open run alone or with the other algorithms of its experiment.
//
// The experiment is made from the chef_cooking_18 fixture: copies with `policy.name` changed
// to robustmpc and mpc, the same video, rate and run number. The fixture has no media, so the
// panes have no engine; layout, run loading and headers are what is tested.

#include "test_paths.h"

#include "compare_view.h"
#include "replay_player.h"
#include "run_pane.h"
#include "session.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/render/render_queue.h"

#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 20000;

bool copyTree(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(info.filePath()));
        if (info.isDir() ? !QDir().mkpath(target) : !QFile::copy(info.filePath(), target))
            return false;
    }
    return true;
}

/// A copy of the fixture as another algorithm's run, named like the delivered bundles.
QString makeRun(const QTemporaryDir& dir, const QString& algorithm)
{
    const QString name = QStringLiteral("chef_cooking_%1_18Mbps_run_3").arg(algorithm);
    const QString target = dir.filePath(name);
    if (!copyTree(test::dataPath(QStringLiteral("chef_cooking_18")), target))
        return {};
    QFile file(QDir(target).filePath(QStringLiteral("run.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonObject run = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonObject policy = run.value(QStringLiteral("policy")).toObject();
    policy.insert(QStringLiteral("name"), algorithm);
    run.insert(QStringLiteral("policy"), policy);
    run.insert(QStringLiteral("run_id"), name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(QJsonDocument(run).toJson());
    return target;
}

} // namespace

class TestCompareView : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        for (const char* algorithm : {"llm", "robustmpc", "mpc"}) {
            const QString run = makeRun(dir_, QString::fromLatin1(algorithm));
            QVERIFY(!run.isEmpty());
            library_ << QSharedPointer<const BundleSummary>::create(readBundleSummary(run));
        }
        for (const BundleSummaryPtr& b : std::as_const(library_))
            QVERIFY2(b->errors.isEmpty(), qPrintable(b->errors.join(u'\n')));
        QCOMPARE(groupExperiments(library_).size(), 1);
    }

    void singleUntilAskedAndWithoutOthers()
    {
        Rig rig;
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        QCOMPARE(rig.view.layoutMode(), CompareView::Layout::Single);
        QCOMPARE(rig.view.paneCount(), 1);
        // Without the library the run has nothing to compare with: Side by side is refused.
        rig.view.setLayoutMode(CompareView::Layout::SideBySide);
        QCOMPARE(rig.view.layoutMode(), CompareView::Layout::Single);
        QCOMPARE(rig.view.paneCount(), 1);
        QVERIFY(rig.view.experimentRuns().isEmpty());
    }

    void sideBySideLoadsTheNextAlgorithm()
    {
        Rig rig;
        rig.view.setLibrary(library_);
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        QCOMPARE(rig.view.experimentRuns().size(), 3);
        QSignalSpy ready(&rig.view, &CompareView::ready);
        rig.view.setLayoutMode(CompareView::Layout::SideBySide);
        QCOMPARE(rig.view.paneCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.view.isReady(), kTimeoutMs);
        // The LLM run is open, so the other pane shows the next algorithm: RobustMPC.
        const auto& other = rig.view.pane(1)->session()->run();
        QVERIFY(other);
        QCOMPARE(other->info.policyName, QStringLiteral("robustmpc"));
        QVERIFY2(rig.view.pane(0)->headerText().startsWith(QStringLiteral("LLM")),
                 qPrintable(rig.view.pane(0)->headerText()));

        // Grid shows every algorithm of the experiment (3 here), Single only the open run.
        rig.view.setLayoutMode(CompareView::Layout::Grid);
        QCOMPARE(rig.view.paneCount(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(rig.view.isReady(), kTimeoutMs);
        QCOMPARE(rig.view.pane(2)->session()->run()->info.policyName, QStringLiteral("mpc"));
        rig.view.setLayoutMode(CompareView::Layout::Single);
        QCOMPARE(rig.view.paneCount(), 1);
    }

    void openingAnotherAlgorithmKeepsTheLayout()
    {
        Rig rig;
        rig.view.setLibrary(library_);
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        rig.view.setLayoutMode(CompareView::Layout::SideBySide);
        QTRY_VERIFY_WITH_TIMEOUT(rig.view.isReady(), kTimeoutMs);
        // Opening RobustMPC keeps Side by side; the other pane must not show RobustMPC again.
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_robustmpc_18Mbps_run_3")));
        QCOMPARE(rig.view.layoutMode(), CompareView::Layout::SideBySide);
        QTRY_VERIFY_WITH_TIMEOUT(rig.view.isReady(), kTimeoutMs);
        QCOMPARE(rig.view.pane(1)->session()->run()->info.policyName, QStringLiteral("llm"));
    }

    void alignmentAndDestructionAreClean()
    {
        auto rig = std::make_unique<Rig>();
        rig->view.setLibrary(library_);
        rig->open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        rig->view.setLayoutMode(CompareView::Layout::Grid);
        rig->view.setAlignment(ComparisonClock::Alignment::MediaTime);
        QCOMPARE(rig->view.alignment(), ComparisonClock::Alignment::MediaTime);
        // Destroyed while the other panes may still be loading.
        rig.reset();
    }

private:
    /// A session, player and comparison view, as the window has them.
    struct Rig {
        RenderQueue queue;
        Session session;
        CompareView view{&session, new ReplayPlayer(&session, &queue), &queue};

        void open(const QString& directory)
        {
            LoadResult result = loadBundleSync(directory);
            QVERIFY2(result.run, qPrintable(result.errors.join(u'\n')));
            session.setRun(result.run, result.errors);
        }
    };

    QTemporaryDir dir_;
    QList<BundleSummaryPtr> library_;
};

QTEST_MAIN(TestCompareView)
#include "tst_compare_view.moc"
