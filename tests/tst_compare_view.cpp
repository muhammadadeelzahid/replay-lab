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

#include <QAbstractItemView>
#include <QComboBox>
#include "charts_panel.h"
#include "scoreboard.h"
#include "wipe_view.h"

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

    void wipeShowsTwoRunsInOneView()
    {
        Rig rig;
        rig.view.setLibrary(library_);
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        rig.view.resize(900, 500);
        rig.view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rig.view));
        rig.view.setLayoutMode(CompareView::Layout::Wipe);
        QCOMPARE(rig.view.paneCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.view.isReady(), kTimeoutMs);
        QVERIFY(rig.view.wipeView()->isVisible());
        QVERIFY(!rig.view.pane(0)->isVisible());
        QVERIFY(!rig.view.pane(1)->isVisible());
        // Back to side by side: the panes return and the Wipe view goes.
        rig.view.setLayoutMode(CompareView::Layout::SideBySide);
        QVERIFY(!rig.view.wipeView()->isVisible());
        QVERIFY(rig.view.pane(1)->isVisible());
    }

    void compareWithPutsTheRunInTheSecondPane()
    {
        Rig rig;
        rig.view.setLibrary(library_);
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        QCOMPARE(rig.view.paneCount(), 1);
        rig.view.compareWith(dir_.filePath(QStringLiteral("chef_cooking_mpc_18Mbps_run_3")));
        QCOMPARE(rig.view.layoutMode(), CompareView::Layout::SideBySide);
        QCOMPARE(rig.view.paneCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(rig.view.isReady(), kTimeoutMs);
        QCOMPARE(rig.view.otherRuns().size(), std::size_t{1});
        QCOMPARE(rig.view.otherRuns().front()->info.policyName, QStringLiteral("mpc"));
        // The run picker's list fits its longest name (it used to be as narrow as the box).
        auto* picker = rig.view.pane(1)->findChild<QComboBox*>();
        QVERIFY(picker && picker->count() >= 2);
        const QFontMetrics metrics(picker->view()->font());
        for (int i = 0; i < picker->count(); ++i)
            QVERIFY(picker->view()->minimumWidth() >= metrics.horizontalAdvance(picker->itemText(i)) + 40);
    }

    void wipeDividerKeysAndLoupe()
    {
        WipeView wipe;
        wipe.resize(800, 450);
        wipe.show();
        QVERIFY(QTest::qWaitForWindowExposed(&wipe));
        QCOMPARE(wipe.divider(), 0.5);
        wipe.setDivider(2.0);
        QCOMPARE(wipe.divider(), 1.0);
        wipe.setDivider(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(wipe.divider(), 0.5);
        // A click puts the divider where it was clicked.
        QTest::mouseClick(wipe.viewport(), Qt::LeftButton, {}, QPoint(200, 200));
        QVERIFY(qAbs(wipe.divider() - 200.0 / wipe.viewport()->width()) < 0.01);
        // Arrow keys move it by 2 %, Shift by 10 %.
        const double before = wipe.divider();
        QTest::keyClick(&wipe, Qt::Key_Right);
        QVERIFY(qAbs(wipe.divider() - (before + 0.02)) < 1e-9);
        QTest::keyClick(&wipe, Qt::Key_Left, Qt::ShiftModifier);
        QVERIFY(qAbs(wipe.divider() - (before + 0.02 - 0.1)) < 1e-9);
        // Moving the pointer shows the loupe; leaving hides it.
        QVERIFY(!wipe.isLoupeVisible());
        QTest::mouseMove(wipe.viewport(), QPoint(400, 200));
        QTRY_VERIFY(wipe.isLoupeVisible());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&wipe, &leave);
        QVERIFY(!wipe.isLoupeVisible());
    }

    void scoreboardRanksEveryAlgorithm()
    {
        Scoreboard board;
        QSignalSpy filled(&board, &Scoreboard::filled);
        const QString open = dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3"));
        board.setExperiment(groupExperiments(library_).front().runs, open);
        QVERIFY(filled.wait(kTimeoutMs) || filled.size() == 1);
        QCOMPARE(board.rowsFilled(), 3);
        QVERIFY(board.cell(0, Scoreboard::Algorithm).startsWith(QStringLiteral("LLM")));
        QVERIFY(board.cell(0, Scoreboard::Algorithm).contains(QStringLiteral("open")));
        QCOMPARE(board.cell(1, Scoreboard::Algorithm), QStringLiteral("RobustMPC"));
        // The copies are the same run, so every row ties and every value is the best.
        for (int row = 0; row < 3; ++row)
            QVERIFY(board.isBest(row, Scoreboard::Stalls));
        // The fixture has no picture-quality scores.
        QCOMPARE(board.cell(0, Scoreboard::VmafMean), QStringLiteral("–"));
        // Fewer than two runs: nothing to compare, the scoreboard hides.
        board.setExperiment({library_.front()}, open);
        QVERIFY(board.isHidden());
    }

    void chartsDrawTheComparedRuns()
    {
        Rig rig;
        ChartsPanel charts(&rig.session);
        rig.open(dir_.filePath(QStringLiteral("chef_cooking_llm_18Mbps_run_3")));
        QCOMPARE(charts.seriesCount(ChartsPanel::Chart::Buffer, QStringLiteral("buffer RobustMPC")), 0);
        const LoadResult other = loadBundleSync(dir_.filePath(QStringLiteral("chef_cooking_robustmpc_18Mbps_run_3")));
        QVERIFY(other.run);
        charts.setComparison({other.run});
        QCOMPARE(charts.seriesCount(ChartsPanel::Chart::Buffer, QStringLiteral("buffer RobustMPC")), 1);
        QVERIFY(charts.seriesCount(ChartsPanel::Chart::Bitrate, QStringLiteral("played RobustMPC")) >= 1);
        // Without picture-quality scores the VMAF chart is hidden.
        QVERIFY(!charts.isShown(ChartsPanel::Chart::Vmaf));
        charts.setComparison({});
        QCOMPARE(charts.seriesCount(ChartsPanel::Chart::Buffer, QStringLiteral("buffer RobustMPC")), 0);
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
