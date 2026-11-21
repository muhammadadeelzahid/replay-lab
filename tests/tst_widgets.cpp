#include "synthetic_run.h"
#include "test_paths.h"

#include "library_view.h"
#include "main_window.h"
#include "moment_panel.h"
#include "network_path_view.h"
#include "replay_player.h"
#include "report_view.h"
#include "segment_timeline.h"
#include "segmented_control.h"
#include "session.h"
#include "settings_dialog.h"
#include "source_list.h"
#include "time_cursor.h"
#include "transport_bar.h"

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/load/bundle_reader.h"
#include "abrviz/core/render/render_queue.h"

#include <QAction>
#include <QCheckBox>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace abrviz;

namespace {

constexpr int kTimeoutMs = 20000;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

std::shared_ptr<const RunData> load(const QString& directory)
{
    return loadBundleSync(directory).run;
}

QString bbb() { return test::dataPath(QStringLiteral("bbb_36")); }
QString chef() { return test::dataPath(QStringLiteral("chef_cooking_18")); }

/// A run with nothing in it: no segments, no playback, no stalls, no topology.
std::shared_ptr<const RunData> emptyRun()
{
    auto run = std::make_shared<RunData>();
    run->info.runId = QStringLiteral("empty");
    return run;
}

/// The synthetic run, with segment downloads that span its whole playback (the synthetic run
/// itself has none), so the network path view has something in flight to animate.
std::shared_ptr<const RunData> downloadingRun()
{
    auto run = std::make_shared<RunData>(*test::syntheticRun(1.0, QStringLiteral("downloading")));
    const double starts[] = {0.5, 1.0, 4.3};
    const double ends[] = {1.0, 4.3, 9.0};
    for (std::size_t i = 0; i < run->segments.size(); ++i) {
        run->segments[i].requestTime = starts[i];
        run->segments[i].doneTime = ends[i];
        run->segments[i].played = true;
    }
    return run;
}

struct NamedRun {
    QString name;
    std::shared_ptr<const RunData> run;
};

/// No run, an empty run, the synthetic run, both test bundles, and one full bundle if present.
std::vector<NamedRun> allRuns()
{
    std::vector<NamedRun> runs{{QStringLiteral("none"), nullptr},
                               {QStringLiteral("empty"), emptyRun()},
                               {QStringLiteral("synthetic"), test::syntheticRun()},
                               {QStringLiteral("bbb_36"), load(bbb())},
                               {QStringLiteral("chef_18"), load(chef())}};
    if (test::haveFullBundles())
        runs.push_back({QStringLiteral("full"),
                        load(QDir(test::bundlesPath()).filePath(QStringLiteral("big_buck_bunny_llm_36Mbps_run_2")))});
    return runs;
}

/// A dark appearance, as macOS gives it.
QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x1e, 0x1e, 0x1e);
    const QColor text(0xf0, 0xf0, 0xf0);
    for (const auto role : {QPalette::Window, QPalette::Base, QPalette::Button, QPalette::AlternateBase})
        p.setColor(role, window);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(role, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x98, 0x98, 0x9d));
    p.setColor(QPalette::Highlight, QColor(0x0a, 0x84, 0xff));
    return p;
}

QPalette lightPalette()
{
    QPalette p;
    const QColor window(0xec, 0xec, 0xec);
    for (const auto role : {QPalette::Window, QPalette::Button, QPalette::AlternateBase})
        p.setColor(role, window);
    p.setColor(QPalette::Base, Qt::white);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(role, QColor(0x1d, 0x1d, 0x1f));
    p.setColor(QPalette::PlaceholderText, QColor(0x86, 0x86, 0x8b));
    p.setColor(QPalette::Highlight, QColor(0x00, 0x7a, 0xff));
    return p;
}

/// Number of strongly red pixels (the stall colour #ff453a and its antialiased edges).
int redPixels(const QImage& image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    int count = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb c = line[x];
            if (qRed(c) > 200 && qGreen(c) < 110 && qBlue(c) < 100)
                ++count;
        }
    }
    return count;
}

/// Paints `widget` into an image with a device pixel ratio, as a Retina screen would.
QImage renderAt(QWidget* widget, qreal ratio)
{
    const QSize size = widget->size();
    QImage image((QSizeF(size) * ratio).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget->render(&painter);
    painter.end();
    return image;
}

/// True if a value string shows a broken number.
bool looksBroken(const QString& text)
{
    const QString lower = text.toLower();
    return lower.contains(QLatin1String("nan")) || lower.contains(QLatin1String("inf"));
}

/// A mouse event sent directly, with an explicit button state (QTest::mouseMove cannot hold a
/// button in Qt 6).
void sendMouse(QWidget* widget, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

/// Whether the view's dash animation timer runs.
bool animating(const NetworkPathView* view)
{
    const auto* timer = view->findChild<QTimer*>();
    return timer && timer->isActive();
}

BundleSummaryPtr summary(const QString& directory)
{
    return BundleSummaryPtr::create(readBundleSummary(directory));
}

BundleSummaryPtr fakeSummary(const QString& directory, const QString& title, std::optional<qint64> rate,
                             QStringList errors = {})
{
    auto s = QSharedPointer<BundleSummary>::create();
    s->directory = directory;
    s->runId = QDir(directory).dirName();
    s->videoName = title.toLower();
    s->videoTitle = title;
    s->linkRate = rate;
    s->segments = 10;
    s->errors = std::move(errors);
    return s;
}

/// The session-driven widgets of the Replay view, around one session and one player without an
/// engine (no rendering).
struct Rig {
    QTemporaryDir cache;
    RenderQueue queue{RenderQueue::Options{cache.path(), QString(), 1}};
    Session session;
    ReplayPlayer player{&session, &queue};
    QAction play{QStringLiteral("Play")};
    QAction back{QStringLiteral("Back")};
    QAction forward{QStringLiteral("Forward")};
    QAction previous{QStringLiteral("Previous")};
    QAction next{QStringLiteral("Next")};
    std::unique_ptr<NetworkPathView> path = std::make_unique<NetworkPathView>(&session, &player);
    std::unique_ptr<MomentPanel> moment = std::make_unique<MomentPanel>(&session, &player);
    SegmentTimeline* timeline = new SegmentTimeline(&session);  // owned by transport
    std::unique_ptr<TransportBar> transport = std::make_unique<TransportBar>(
        &session, &player, timeline, TransportBar::Actions{&previous, &back, &play, &forward, &next});
    std::unique_ptr<ReportView> report = std::make_unique<ReportView>(&session);

    ~Rig()
    {
        // Widgets that read the session go first; then the engine, before the queue.
        report.reset();
        transport.reset();
        moment.reset();
        path.reset();
        player.releaseEngine();
    }

    [[nodiscard]] std::vector<QWidget*> widgets() const
    {
        return {path.get(), moment.get(), timeline, transport.get(), report.get()};
    }
};

/// Checks what every widget of `rig` shows for the open run (or its absence).
void checkConsistent(const Rig& rig)
{
    const auto& run = rig.session.run();
    if (!run) {
        QVERIFY(rig.path->activityText().isEmpty());
        QVERIFY(!rig.path->isDownloading());
        QCOMPARE(rig.path->labels().size(), 7);
        QCOMPARE(rig.path->labels().last(), QStringLiteral("UE"));
        for (const QString& field : {QStringLiteral("Segment"), QStringLiteral("Buffer"), QStringLiteral("Player"),
                                     QStringLiteral("Engine"), QStringLiteral("Satellite")})
            QCOMPARE(rig.moment->value(field), QStringLiteral("–"));
        QVERIFY(rig.timeline->items().empty());
        QCOMPARE(rig.transport->timeText(), QStringLiteral(" "));
        QVERIFY(rig.transport->stateText().isEmpty());
        QCOMPARE(rig.report->verdict(), QStringLiteral("No run open."));
        QVERIFY(rig.report->rows().isEmpty());
        return;
    }
    QVERIFY(!rig.path->activityText().isEmpty());
    QCOMPARE(rig.moment->value(QStringLiteral("Engine")), QStringLiteral("none"));
    QVERIFY(!rig.moment->value(QStringLiteral("Simulation")).isEmpty());
    QVERIFY(rig.transport->timeText().contains(QLatin1Char(':')));
    QVERIFY(rig.transport->stateText().startsWith(QStringLiteral("Idle · sim ")));
    QVERIFY(!rig.report->rows().isEmpty());
    QVERIFY(!rig.report->verdict().isEmpty());
    for (const QString& row : rig.report->rows())
        QVERIFY2(!looksBroken(row), qPrintable(row));
    QCOMPARE(rig.timeline->count(SegmentTimeline::Kind::Stall), static_cast<int>(run->stalls.size()));
    QCOMPARE(rig.timeline->count(SegmentTimeline::Kind::Download), static_cast<int>(run->segments.size()));
}

/// The cursor values that tend to break things for a run: before the start, the boundaries of
/// playback and of each stall, the end, past the end, and non-finite values.
std::vector<double> edgeTimes(const Session& session)
{
    std::vector<double> times{-1.0, 0.0, -0.0, 1e-12, session.endTime(), session.endTime() + 1.0, 1e300,
                              kNaN, kInf, -kInf};
    if (const auto& run = session.run()) {
        times.push_back(run->streamStart);
        times.push_back(run->playbackStart);
        times.push_back(run->playbackEnd);
        for (const Stall& stall : run->stalls) {
            for (const double t : {stall.start, stall.end}) {
                times.push_back(std::nextafter(t, -kInf));
                times.push_back(t);
                times.push_back(std::nextafter(t, kInf));
            }
        }
    }
    return times;
}

} // namespace

class TestWidgets : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Never put a window on the user's screen.
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("run with QT_QPA_PLATFORM=offscreen");
    }

    // --- TimeCursor and Session ---------------------------------------------------------------

    void cursorIgnoresNaNAndClampsInfinities()
    {
        TimeCursor cursor;
        cursor.setRange(1.0, 9.0);
        QSignalSpy changed(&cursor, &TimeCursor::timeChanged);
        cursor.setTime(kNaN, TimeCursor::Source::Timeline);
        QCOMPARE(cursor.time(), 1.0);
        QCOMPARE(changed.size(), 0);
        cursor.setTime(kInf);
        QCOMPARE(cursor.time(), 9.0);
        cursor.setTime(-kInf);
        QCOMPARE(cursor.time(), 1.0);
        QCOMPARE(changed.size(), 2);
        cursor.setTime(-kInf);
        cursor.setTime(1.0);
        QCOMPARE(changed.size(), 2);

        // An inverted range collapses to its minimum and moves the cursor there once.
        cursor.setTime(5.0);
        changed.clear();
        cursor.setRange(7.0, 3.0);
        QCOMPARE(cursor.minimum(), 7.0);
        QCOMPARE(cursor.maximum(), 7.0);
        QCOMPARE(cursor.time(), 7.0);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(1).value<TimeCursor::Source>(), TimeCursor::Source::Program);
        cursor.setRange(7.0, 3.0);
        QCOMPARE(changed.size(), 1);
    }

    void sessionSignalsOncePerSetRun()
    {
        Session session;
        QSignalSpy runChanged(&session, &Session::runChanged);
        QSignalSpy indexChanged(&session, &Session::logIndexChanged);
        QSignalSpy timeChanged(session.cursor(), &TimeCursor::timeChanged);

        const auto run = test::syntheticRun();
        session.setRun(run);
        QCOMPARE(runChanged.size(), 1);
        QCOMPARE(indexChanged.size(), 0);
        QCOMPARE(session.cursor()->time(), run->playbackStart);
        QCOMPARE(session.cursor()->minimum(), 0.0);
        QCOMPARE(session.cursor()->maximum(), session.endTime());
        QCOMPARE(timeChanged.size(), 1);

        // An index for another run is dropped without a signal.
        const auto other = test::syntheticRun();
        session.setLogIndex(LogIndex::fromText({}, {}, 0), other.get());
        QVERIFY(!session.logIndex());
        QCOMPARE(indexChanged.size(), 0);
        session.setLogIndex(LogIndex::fromText({}, {}, 0), run.get());
        QVERIFY(session.logIndex());
        QCOMPARE(indexChanged.size(), 1);

        // Clearing drops the index: one runChanged, then one logIndexChanged.
        session.clear();
        QCOMPARE(runChanged.size(), 2);
        QCOMPARE(indexChanged.size(), 2);
        QCOMPARE(session.cursor()->time(), 0.0);
        QCOMPARE(session.endTime(), 0.0);
        QCOMPARE(session.report().stallCount, 0);
        const auto timeChanges = timeChanged.size();

        // Design choice: setRun() always emits runChanged, even when nothing changes (a second
        // clear, or the same run again). The cursor does not move, so no timeChanged.
        session.clear();
        QCOMPARE(runChanged.size(), 3);
        QCOMPARE(indexChanged.size(), 2);
        QCOMPARE(timeChanged.size(), timeChanges);
        session.setLogIndex(LogIndex::fromText({}, {}, 0), nullptr);
        QVERIFY(!session.logIndex());
        QCOMPARE(indexChanged.size(), 2);
    }

    void sessionHoldsNoReferenceAfterClear()
    {
        auto run = std::shared_ptr<const RunData>(load(bbb()));
        QVERIFY(run);
        {
            Rig rig;
            rig.session.setRun(run);
            QVERIFY(run.use_count() > 1);
            rig.session.clear();
            QCOMPARE(run.use_count(), 1L);
            rig.session.setRun(run);
        }
        // The widgets and the session are gone; the run is still referenced here and intact.
        QCOMPARE(run.use_count(), 1L);
        QVERIFY(!run->segments.empty());
    }

    // --- Every session widget, every run --------------------------------------------------------

    void widgetsFollowRunSwitchesAndClears()
    {
        Rig rig;
        for (QWidget* w : rig.widgets())
            w->resize(w->sizeHint().expandedTo(QSize(300, 100)));
        checkConsistent(rig);
        if (QTest::currentTestFailed())
            return;
        const auto runs = allRuns();
        for (int pass = 0; pass < 2; ++pass) {
            for (const NamedRun& named : runs) {
                rig.session.setRun(named.run);
                checkConsistent(rig);
                if (QTest::currentTestFailed()) {
                    qWarning() << "run" << named.name << "pass" << pass;
                    return;
                }
                for (QWidget* w : rig.widgets())
                    QVERIFY(!w->grab().isNull());
                rig.session.clear();
                checkConsistent(rig);
                if (QTest::currentTestFailed())
                    return;
            }
        }
        // Rebuilding the same run gives the same marks and rows, not twice as many.
        rig.session.setRun(load(bbb()));
        const auto items = rig.timeline->items().size();
        const auto rows = rig.report->rows();
        rig.session.setRun(load(bbb()));
        QCOMPARE(rig.timeline->items().size(), items);
        QCOMPARE(rig.report->rows(), rows);
    }

    void cursorAtExtremesNeverBreaksTheWidgets()
    {
        Rig rig;
        for (QWidget* w : rig.widgets())
            w->resize(w->sizeHint().expandedTo(QSize(300, 100)));
        for (const NamedRun& named : allRuns()) {
            rig.session.setRun(named.run);
            for (const double t : edgeTimes(rig.session)) {
                rig.session.cursor()->setTime(t, TimeCursor::Source::Timeline);
                const double now = rig.session.cursor()->time();
                QVERIFY2(std::isfinite(now), qPrintable(named.name));
                QVERIFY(now >= 0.0 && now <= rig.session.endTime());
                const QStringList texts = {rig.path->activityText(), rig.transport->timeText(),
                                           rig.transport->stateText(), rig.moment->value(QStringLiteral("Segment")),
                                           rig.moment->value(QStringLiteral("Buffer")),
                                           rig.moment->value(QStringLiteral("Simulation")),
                                           rig.moment->value(QStringLiteral("Replay"))};
                for (const QString& text : texts)
                    QVERIFY2(!looksBroken(text), qPrintable(QStringLiteral("%1 at %2: %3").arg(named.name).arg(t).arg(text)));
                for (QWidget* w : rig.widgets())
                    QVERIFY(!w->grab().isNull());
            }
        }
    }

    void emptyRunDegradesGracefully()
    {
        Rig rig;
        rig.session.setRun(emptyRun());
        QCOMPARE(rig.session.endTime(), 0.0);
        QCOMPARE(rig.session.cursor()->time(), 0.0);
        QVERIFY(rig.timeline->items().empty());
        QCOMPARE(rig.timeline->xAt(5.0), rig.timeline->xAt(0.0));
        QCOMPARE(rig.timeline->timeAt(1e6), 0.0);
        QVERIFY(!rig.path->isDownloading());
        QCOMPARE(rig.moment->value(QStringLiteral("Satellite")), QStringLiteral("–"));
        QCOMPARE(rig.moment->value(QStringLiteral("At")), QStringLiteral("–"));
        // No summary.json numbers: the verdict must not claim that "all 0" of them agree.
        QVERIFY2(!rig.report->verdict().contains(QStringLiteral("All 0")), qPrintable(rig.report->verdict()));
        QVERIFY(rig.report->verdict().contains(QStringLiteral("summary.json")));
        for (const QString& row : rig.report->rows())
            QVERIFY2(!looksBroken(row), qPrintable(row));
        QVERIFY(rig.transport->timeText().startsWith(QStringLiteral("0:00.0")));
    }

    void pathAndInspectorWithoutTopology()
    {
        Rig rig;
        rig.session.setRun(test::syntheticRun());
        const QStringList labels = rig.path->labels();
        QCOMPARE(labels.mid(0, 6),
                 (QStringList{QStringLiteral("Content server"), QStringLiteral("Feeder"),
                              QStringLiteral("LEO gNB"), QStringLiteral("Backhaul"),
                              QStringLiteral("IAB node"), QStringLiteral("Access")}));
        // The synthetic run has no node id (-1); the viewer is still named.
        QVERIFY(labels[6].startsWith(QStringLiteral("UE")));
        for (const QString& field : {QStringLiteral("Feeder"), QStringLiteral("Satellite"), QStringLiteral("Carrier"),
                                     QStringLiteral("Relay"), QStringLiteral("Estimate"), QStringLiteral("RTT")})
            QCOMPARE(rig.moment->value(field), QStringLiteral("–"));
        QCOMPARE(rig.moment->value(QStringLiteral("No such field")), QString());

        // A bundle with topology fills the same rows, and switching back empties them again.
        rig.session.setRun(load(bbb()));
        QVERIFY(rig.path->labels()[2].contains(QStringLiteral(" km")));
        QVERIFY(rig.moment->value(QStringLiteral("Satellite")).startsWith(QStringLiteral("LEO gNB at ")));
        rig.session.setRun(test::syntheticRun());
        QCOMPARE(rig.moment->value(QStringLiteral("Satellite")), QStringLiteral("–"));
        QCOMPARE(rig.path->labels()[2], QStringLiteral("LEO gNB"));
    }

    void stallStateAgreesAcrossWidgets()
    {
        Rig rig;
        rig.path->resize(900, 150);
        rig.session.setRun(test::syntheticRun(1.0));
        const Stall stall = rig.session.run()->stalls.front();
        const auto stalledInPath = [&] { return rig.path->activityText().startsWith(QStringLiteral("Stalled")); };
        const auto stalledInPanel = [&] { return rig.moment->value(QStringLiteral("Player")).contains(QStringLiteral("stalled")); };
        for (const double t : {stall.start - 0.5, std::nextafter(stall.start, -kInf), stall.start,
                               (stall.start + stall.end) / 2, std::nextafter(stall.end, -kInf), stall.end,
                               stall.end + 0.1}) {
            rig.session.cursor()->setTime(t);
            QVERIFY2(stalledInPath() == stalledInPanel(), qPrintable(QString::number(t, 'g', 17)));
        }
        rig.session.cursor()->setTime(stall.start - 0.5);
        QVERIFY(!stalledInPath());
        // Outside a stall the only red is the relay mast's small warning light.
        const int calm = redPixels(rig.path->grab().toImage());
        QVERIFY2(calm < 20, qPrintable(QString::number(calm)));

        // Mid-stall the user is drawn red, in both appearances.
        rig.session.cursor()->setTime((stall.start + stall.end) / 2);
        QVERIFY(stalledInPath());
        QVERIFY(redPixels(rig.path->grab().toImage()) > calm + 20);
        rig.path->setPalette(darkPalette());
        QVERIFY(redPixels(rig.path->grab().toImage()) > calm + 20);

        // Clearing the run takes the red away.
        rig.session.clear();
        QVERIFY(redPixels(rig.path->grab().toImage()) <= calm);
    }

    // --- Painting -------------------------------------------------------------------------------

    void paintingSurvivesExtremeSizesPalettesAndRatios()
    {
        Rig rig;
        rig.session.setRun(load(bbb()));
        rig.session.cursor()->setTime(rig.session.endTime() / 2);

        NetworkPathView staticPath(nullptr, nullptr);
        SegmentedControl views;
        for (const char* name : {"Replay", "Analysis", "Logs", "Report"})
            views.addSegment(QString::fromLatin1(name));
        views.setCurrentIndex(1);
        SourceList sidebar;
        sidebar.setRuns({summary(bbb()), summary(chef())});
        LibraryView welcome;
        welcome.setRecentFolders({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/ünïcødé")});
        LibraryView table;
        table.setBundles({summary(bbb()), summary(chef())}, test::dataPath(QString()));
        QTemporaryDir cache;
        SettingsDialog settings(cache.path());

        std::vector<QWidget*> widgets = rig.widgets();
        for (QWidget* w : std::initializer_list<QWidget*>{&staticPath, &views, &sidebar, &welcome, &table, &settings})
            widgets.push_back(w);

        for (const QPalette& palette : {lightPalette(), darkPalette()}) {
            for (QWidget* w : widgets) {
                w->setPalette(palette);
                for (const QSize size : {QSize(1, 1), QSize(10, 10), QSize(4000, 3000)}) {
                    w->resize(size);
                    const QPixmap pixmap = w->grab();
                    QVERIFY2(!pixmap.isNull(), w->metaObject()->className());
                }
                w->resize(w->sizeHint().expandedTo(QSize(50, 50)));
                for (const qreal ratio : {1.0, 2.0, 3.0}) {
                    const QImage image = renderAt(w, ratio);
                    QVERIFY(!image.isNull());
                    QCOMPARE(image.devicePixelRatio(), ratio);
                }
            }
        }
    }

    void restylingOnPaletteChangeDoesNotLoop()
    {
        SegmentedControl views;
        views.addSegment(QStringLiteral("A"));
        SourceList sidebar;
        const QString lightViews = views.styleSheet();
        const QString lightSidebar = sidebar.styleSheet();
        QVERIFY(lightSidebar.contains(QStringLiteral("#ececee")));
        for (int i = 0; i < 20; ++i) {
            views.setPalette(i % 2 ? lightPalette() : darkPalette());
            sidebar.setPalette(i % 2 ? lightPalette() : darkPalette());
        }
        views.setPalette(darkPalette());
        sidebar.setPalette(darkPalette());
        QVERIFY(views.styleSheet() != lightViews);
        // In the dark theme the sidebar is darker than the window.
        QVERIFY(sidebar.styleSheet().contains(darkPalette().color(QPalette::Window).darker(150).name()));
        sidebar.setPalette(lightPalette());
        QVERIFY(sidebar.styleSheet().contains(QStringLiteral("#ececee")));
    }

    // --- SegmentTimeline -------------------------------------------------------------------------

    void timelineMouseAtEdgesAndOutside()
    {
        Session session;
        SegmentTimeline timeline(&session);
        timeline.resize(600, timeline.sizeHint().height());
        timeline.show();
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        TimeCursor* cursor = session.cursor();
        QSignalSpy changed(cursor, &TimeCursor::timeChanged);

        // Without a run, nothing moves.
        QTest::mouseClick(&timeline, Qt::LeftButton, {}, QPoint(300, 20));
        QCOMPARE(changed.size(), 0);

        session.setRun(load(bbb()));
        changed.clear();
        const double end = session.endTime();
        const int y = 20;

        // Far outside on the left: the start; far outside on the right: the end.
        QTest::mouseClick(&timeline, Qt::LeftButton, {}, QPoint(-500, y));
        QCOMPARE(cursor->time(), 0.0);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.last().at(1).value<TimeCursor::Source>(), TimeCursor::Source::Timeline);
        QTest::mouseClick(&timeline, Qt::LeftButton, {}, QPoint(timeline.width() + 500, y));
        QCOMPARE(cursor->time(), end);
        QCOMPARE(changed.size(), 2);
        // The same place again is a no-op: no signal.
        QTest::mouseClick(&timeline, Qt::LeftButton, {}, QPoint(timeline.width() + 900, y));
        QCOMPARE(changed.size(), 2);
        // The label column left of the plot is time 0 too.
        QTest::mouseClick(&timeline, Qt::LeftButton, {}, QPoint(0, y));
        QCOMPARE(cursor->time(), 0.0);
        QCOMPARE(changed.size(), 3);

        // Right and middle buttons do nothing.
        QTest::mouseClick(&timeline, Qt::RightButton, {}, QPoint(400, y));
        QTest::mouseClick(&timeline, Qt::MiddleButton, {}, QPoint(400, y));
        QCOMPARE(changed.size(), 3);

        // A drag from the middle out past the right edge and back past the left edge.
        const QPointF middle(timeline.xAt(end / 2), y);
        sendMouse(&timeline, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton);
        QVERIFY(std::abs(cursor->time() - end / 2) < end / 100);
        sendMouse(&timeline, QEvent::MouseMove, QPointF(timeline.width() * 3.0, -200), Qt::NoButton, Qt::LeftButton);
        QCOMPARE(cursor->time(), end);
        sendMouse(&timeline, QEvent::MouseMove, QPointF(-1e6, 1e6), Qt::NoButton, Qt::LeftButton);
        QCOMPARE(cursor->time(), 0.0);
        sendMouse(&timeline, QEvent::MouseButtonRelease, QPointF(-1e6, 1e6), Qt::LeftButton, Qt::NoButton);
        const auto afterDrag = changed.size();
        // Moving without a button held does not scrub.
        sendMouse(&timeline, QEvent::MouseMove, middle, Qt::NoButton, Qt::NoButton);
        QCOMPARE(changed.size(), afterDrag);

        // The wheel does not move the cursor.
        QWheelEvent wheel(middle, timeline.mapToGlobal(middle), QPoint(0, 120), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&timeline, &wheel);
        QWheelEvent sideways(middle, timeline.mapToGlobal(middle), QPoint(-500, 0), QPoint(-500, 0), Qt::NoButton,
                             Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&timeline, &sideways);
        QCOMPARE(changed.size(), afterDrag);

        // Clearing the run while the button is held: further moves do nothing.
        sendMouse(&timeline, QEvent::MouseButtonPress, middle, Qt::LeftButton, Qt::LeftButton);
        session.clear();
        changed.clear();
        sendMouse(&timeline, QEvent::MouseMove, QPointF(500, y), Qt::NoButton, Qt::LeftButton);
        sendMouse(&timeline, QEvent::MouseButtonRelease, QPointF(500, y), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(changed.size(), 0);
    }

    void timelineGeometryAndToolTipsAtEdges()
    {
        Session session;
        SegmentTimeline timeline(&session);
        timeline.resize(1, 1);
        QCOMPARE(timeline.timeAt(100.0), 0.0);  // no run: the axis has no length
        QCOMPARE(timeline.itemAt(QPointF(0, 0)), nullptr);
        session.setRun(load(bbb()));
        const double end = session.endTime();
        // A one-pixel widget still maps finitely and clamps.
        for (const double x : {-1e9, -1.0, 0.0, 0.5, 1.0, 1e9}) {
            const double t = timeline.timeAt(x);
            QVERIFY(std::isfinite(t));
            QVERIFY(t >= 0.0 && t <= end);
        }
        QVERIFY(std::isfinite(timeline.xAt(end)));
        QVERIFY(!timeline.grab().isNull());

        timeline.resize(1200, timeline.sizeHint().height());
        QCOMPARE(timeline.timeAt(timeline.xAt(0.0)), 0.0);
        QVERIFY(std::abs(timeline.timeAt(timeline.xAt(end)) - end) < 1e-9);
        QCOMPARE(timeline.itemAt(QPointF(-10, 20)), nullptr);
        QCOMPARE(timeline.itemAt(QPointF(timeline.width() + 10.0, 20)), nullptr);
        QCOMPARE(timeline.itemAt(QPointF(timeline.width() / 2.0, 1e6)), nullptr);

        // Tool-tip events are handled over a mark and over empty space.
        const auto& stall = *std::ranges::find(timeline.items(), SegmentTimeline::Kind::Stall, &SegmentTimeline::Item::kind);
        const QPoint over(static_cast<int>(std::lround(timeline.xAt((stall.start + stall.end) / 2))), 6 + 32 + 13);
        for (const QPoint pos : {over, QPoint(5, 5), QPoint(-50, -50)}) {
            QHelpEvent help(QEvent::ToolTip, pos, timeline.mapToGlobal(pos));
            QVERIFY(QCoreApplication::sendEvent(&timeline, &help));
        }
    }

    // --- TransportBar ----------------------------------------------------------------------------

    void transportTimeNeverShowsSixtySeconds()
    {
        Rig rig;
        rig.session.setRun(load(bbb()));
        const PlaybackClock& clock = rig.session.clock();
        QVERIFY2(clock.replayDuration() > 61.0, qPrintable(QString::number(clock.replayDuration())));
        rig.session.cursor()->setTime(clock.replayToSim(0.0));
        QVERIFY2(rig.transport->timeText().startsWith(QStringLiteral("0:00.0 -")), qPrintable(rig.transport->timeText()));
        // Replay times that round up to a whole minute must carry into the minutes.
        const QRegularExpression sixty(QStringLiteral(":6\\d\\.\\d"));
        for (const double replay : {59.94, 59.95, 59.96, 59.99, 119.951, 119.99}) {
            if (replay > clock.replayDuration())
                continue;
            rig.session.cursor()->setTime(clock.replayToSim(replay));
            const QString text = rig.transport->timeText();
            QVERIFY2(!text.contains(sixty), qPrintable(QStringLiteral("%1: %2").arg(replay).arg(text)));
        }
        rig.session.cursor()->setTime(clock.replayToSim(59.99));
        QVERIFY2(rig.transport->timeText().startsWith(QStringLiteral("1:00.0")), qPrintable(rig.transport->timeText()));
        // The remaining time at the very end is zero, never negative.
        rig.session.cursor()->setTime(rig.session.endTime());
        QVERIFY2(rig.transport->timeText().endsWith(QStringLiteral(" -0:00.0")), qPrintable(rig.transport->timeText()));
        for (double replay = 0.0; replay < clock.replayDuration(); replay += 0.01) {
            rig.session.cursor()->setTime(clock.replayToSim(replay));
            QVERIFY2(!rig.transport->timeText().contains(sixty), qPrintable(rig.transport->timeText()));
        }
    }

    void transportWithoutActionsAndThroughRunSwitches()
    {
        QTemporaryDir cache;
        RenderQueue queue(RenderQueue::Options{cache.path(), QString(), 1});
        Session session;
        ReplayPlayer player(&session, &queue);
        {
            // No actions at all: buttons without a default action.
            TransportBar bar(&session, &player, new SegmentTimeline(&session), TransportBar::Actions{});
            bar.resize(900, 120);
            QVERIFY(!bar.grab().isNull());
            session.setRun(load(chef()));
            QVERIFY(!bar.timeText().trimmed().isEmpty());
            for (QToolButton* button : bar.findChildren<QToolButton*>())
                QTest::mouseClick(button, Qt::LeftButton);
            session.clear();
            QCOMPARE(bar.timeText(), QStringLiteral(" "));
        }
        player.releaseEngine();
    }

    void transportButtonsTriggerTheirActionsOnce()
    {
        Rig rig;
        rig.session.setRun(load(bbb()));
        rig.transport->resize(900, 120);
        QSignalSpy play(&rig.play, &QAction::triggered);
        QSignalSpy next(&rig.next, &QAction::triggered);
        QToolButton* playButton = rig.transport->findChild<QToolButton*>(QStringLiteral("playButton"));
        QVERIFY(playButton);
        QCOMPARE(playButton->defaultAction(), &rig.play);
        QTest::mouseClick(playButton, Qt::LeftButton);
        QCOMPARE(play.size(), 1);
        QCOMPARE(next.size(), 0);
        // Pressing inside and releasing outside cancels the click.
        QTest::mousePress(playButton, Qt::LeftButton, {}, QPoint(2, 2));
        QTest::mouseRelease(playButton, Qt::LeftButton, {}, QPoint(-100, -100));
        QCOMPARE(play.size(), 1);
        // A disabled action's button does nothing.
        rig.play.setEnabled(false);
        QTest::mouseClick(playButton, Qt::LeftButton);
        QCOMPARE(play.size(), 1);
        // The buttons take no keyboard focus, so keys go to the window's shortcuts instead.
        for (QToolButton* button : rig.transport->findChildren<QToolButton*>())
            QCOMPARE(button->focusPolicy(), Qt::NoFocus);
    }

    void mainWindowShortcutsMoveTheCursor()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        if (!QTest::qWaitForWindowActive(&window))
            QSKIP("the platform cannot activate windows");
        QSignalSpy opened(&window, &MainWindow::bundleOpened);
        window.openBundle(bbb());
        QVERIFY(!opened.isEmpty() || opened.wait(kTimeoutMs));
        QCOMPARE(window.currentPage(), MainWindow::Page::Replay);

        Session* session = window.session();
        const PlaybackClock& clock = session->clock();
        TimeCursor* cursor = session->cursor();
        cursor->setTime(clock.replayToSim(0.0));
        QSignalSpy changed(cursor, &TimeCursor::timeChanged);

        // Back at the start: clamped, no move, no signal.
        QTest::keyClick(&window, Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(changed.size(), 0);
        QTest::keyClick(&window, Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(changed.size(), 1);
        QVERIFY(std::abs(clock.simToReplay(cursor->time()) - 5.0) < 0.01);

        // Forward at the end: no move, no signal.
        cursor->setTime(session->endTime());
        changed.clear();
        QTest::keyClick(&window, Qt::Key_Right, Qt::AltModifier);
        QTest::keyClick(&window, Qt::Key_BracketRight, Qt::ControlModifier);
        QCOMPARE(changed.size(), 0);

        // Page shortcuts, including the one for the page already shown.
        QTest::keyClick(&window, Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), MainWindow::Page::Analysis);
        QTest::keyClick(&window, Qt::Key_3, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), MainWindow::Page::Analysis);
        QTest::keyClick(&window, Qt::Key_5, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), MainWindow::Page::Report);
        QTest::keyClick(&window, Qt::Key_2, Qt::ControlModifier);
        QCOMPARE(window.currentPage(), MainWindow::Page::Replay);
        window.shutdown();
    }

    // --- SegmentedControl ------------------------------------------------------------------------

    void segmentedControlSelection()
    {
        SegmentedControl control;
        QCOMPARE(control.count(), 0);
        QCOMPARE(control.currentIndex(), -1);
        control.setCurrentIndex(0);  // nothing to select
        control.setSegmentEnabled(3, false);
        QCOMPARE(control.segmentText(0), QString());
        QCOMPARE(control.segmentText(-1), QString());

        QCOMPARE(control.addSegment(QStringLiteral("Replay")), 0);
        QCOMPARE(control.addSegment(QStringLiteral("Analysis"), QStringLiteral("⌘3")), 1);
        QCOMPARE(control.addSegment(QString()), 2);
        QCOMPARE(control.addSegment(QStringLiteral("Lögs 🐰 %1 & <b>")), 3);
        QCOMPARE(control.count(), 4);
        QCOMPARE(control.segmentText(3), QStringLiteral("Lögs 🐰 %1 & <b>"));
        control.resize(control.sizeHint());
        control.show();
        QVERIFY(QTest::qWaitForWindowExposed(&control));

        QSignalSpy changed(&control, &SegmentedControl::currentChanged);
        control.setCurrentIndex(1);
        QCOMPARE(control.currentIndex(), 1);
        control.setCurrentIndex(99);
        QCOMPARE(control.currentIndex(), -1);
        control.setCurrentIndex(-5);
        QCOMPARE(control.currentIndex(), -1);
        control.setCurrentIndex(2);
        QCOMPARE(changed.size(), 0);  // never from setCurrentIndex()

        const auto buttons = control.findChildren<QToolButton*>();
        QCOMPARE(buttons.size(), 4);
        QTest::mouseClick(buttons[0], Qt::LeftButton);
        QCOMPARE(control.currentIndex(), 0);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.last().at(0).toInt(), 0);
        // Design choice: clicking the selected segment emits again (the window just shows the
        // page it already shows).
        QTest::mouseClick(buttons[0], Qt::LeftButton);
        QCOMPARE(control.currentIndex(), 0);
        QCOMPARE(changed.size(), 2);

        // A disabled segment cannot be selected by clicking.
        control.setSegmentEnabled(1, false);
        QTest::mouseClick(buttons[1], Qt::LeftButton);
        QCOMPARE(control.currentIndex(), 0);
        QCOMPARE(changed.size(), 2);
        control.setSegmentEnabled(1, true);

        // The segments take no focus: arrow keys neither move the selection nor emit.
        for (QToolButton* button : buttons)
            QCOMPARE(button->focusPolicy(), Qt::NoFocus);
        for (const auto key : {Qt::Key_Right, Qt::Key_Left, Qt::Key_Space, Qt::Key_Return, Qt::Key_Tab})
            QTest::keyClick(&control, key);
        QCOMPARE(control.currentIndex(), 0);
        QCOMPARE(changed.size(), 2);

        // With nothing selected a click selects again.
        control.setCurrentIndex(-1);
        QTest::mouseClick(buttons[3], Qt::LeftButton);
        QCOMPARE(control.currentIndex(), 3);
        QCOMPARE(changed.size(), 3);
    }

    // --- SourceList ------------------------------------------------------------------------------

    void sourceListKeyboardAtFirstAndLastRow()
    {
        SourceList list;
        list.resize(240, 600);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        QSignalSpy all(&list, &SourceList::allRunsSelected);
        QSignalSpy run(&list, &SourceList::runSelected);

        const QString broken = QStringLiteral("/nonexistent/broken_run");
        const QString last = QStringLiteral("/nonexistent/zz_last");
        list.setRuns({fakeSummary(QStringLiteral("/nonexistent/a"), QStringLiteral("Alpha"), 9'000'000),
                      fakeSummary(broken, QStringLiteral("Alpha"), 18'000'000, {QStringLiteral("run.json: bad")}),
                      fakeSummary(last, QStringLiteral("Omega 🎬"), std::nullopt)});
        QCOMPARE(list.runCount(), 3);
        QCOMPARE(list.runLabel(last), QStringLiteral("Omega 🎬 / Uncongested"));
        QCOMPARE(list.runLabel(QStringLiteral("/not/listed")), QString());
        QCOMPARE(all.size() + run.size(), 0);  // setRuns() is silent

        list.selectAllRuns();
        QCOMPARE(all.size(), 0);  // so is selectAllRuns()
        // Up from the first selectable row: nothing above is a run.
        QTest::keyClick(list.viewport(), Qt::Key_Up);
        QTest::keyClick(&list, Qt::Key_Up);
        QTest::keyClick(&list, Qt::Key_Home);
        QCOMPARE(run.size(), 0);

        // The last row, then past it.
        list.selectRun(last);
        QCOMPARE(run.size(), 0);  // selectRun() is silent
        QTest::keyClick(&list, Qt::Key_Down);
        QTest::keyClick(&list, Qt::Key_End);
        QTest::keyClick(&list, Qt::Key_PageDown);
        QVERIFY(run.isEmpty() || run.last().at(0).toString() == last);
        QCOMPARE(all.size(), 0);

        // Walking up from the last run never selects the disabled run.
        run.clear();
        list.selectRun(last);
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(&list, Qt::Key_Up);
        for (const QList<QVariant>& args : run)
            QVERIFY(args.at(0).toString() != broken);

        // A run that is not listed leaves the selection alone; an empty list keeps All Runs.
        list.selectRun(last);
        QTreeWidgetItem* current = list.currentItem();
        list.selectRun(QStringLiteral("/not/listed"));
        QCOMPARE(list.currentItem(), current);
        list.setRuns({});
        QCOMPARE(list.runCount(), 0);
        QCOMPARE(list.runLabel(last), QString());
        list.selectRun(last);
        list.selectAllRuns();
        QVERIFY(list.currentItem());
        QCOMPARE(list.currentItem()->text(0), QStringLiteral("All Runs"));
    }

    void sourceListMouseSelectionEmitsOnce()
    {
        SourceList list;
        list.resize(240, 600);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        list.setRuns({summary(bbb()), summary(chef())});
        QCOMPARE(list.runCount(), 2);
        QSignalSpy all(&list, &SourceList::allRunsSelected);
        QSignalSpy run(&list, &SourceList::runSelected);

        QTreeWidgetItem* bbbItem = list.topLevelItem(1)->child(0);
        const QPoint at = list.visualItemRect(bbbItem).center();
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, at);
        QCOMPARE(run.size(), 1);
        QCOMPARE(run.first().at(0).toString(), bbbItem->data(0, Qt::UserRole + 2).toString());
        // Clicking the selected run again is not a change.
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, at);
        QCOMPARE(run.size(), 1);
        // Clicking a section heading neither selects it nor emits.
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, list.visualItemRect(list.topLevelItem(1)).center());
        QCOMPARE(run.size(), 1);
        QCOMPARE(all.size(), 0);
        // Below the last row: nothing.
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, QPoint(10, list.viewport()->height() - 2));
        QCOMPARE(run.size(), 1);
        QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, list.visualItemRect(list.topLevelItem(0)->child(0)).center());
        QCOMPARE(all.size(), 1);
    }

    // --- LibraryView and the welcome screen ------------------------------------------------------

    void libraryFilterTakesTextLiterally()
    {
        LibraryView view;
        view.resize(1000, 600);
        view.setBundles({summary(bbb()), summary(chef()),
                         fakeSummary(QStringLiteral("/nonexistent/x"), QStringLiteral("Regex (a|b)* [x] ^$ \\d+ .?"),
                                     1'000'000)},
                        test::dataPath(QString()));
        QCOMPARE(view.visibleRows(), 3);
        LibraryModel* model = view.model();
        // What a literal, case-insensitive search should find.
        const auto expected = [model](const QString& text) {
            int rows = 0;
            for (int r = 0; r < model->rowCount(); ++r) {
                bool hit = false;
                for (int c = 0; c < model->columnCount(); ++c)
                    hit = hit || model->data(model->index(r, c), Qt::DisplayRole).toString().contains(text, Qt::CaseInsensitive);
                rows += hit ? 1 : 0;
            }
            return rows;
        };
        const QStringList patterns = {
            QString(), QStringLiteral("("), QStringLiteral(")"), QStringLiteral("["), QStringLiteral("*"),
            QStringLiteral("\\"), QStringLiteral(".*"), QStringLiteral("a|b"), QStringLiteral("(a|b)*"),
            QStringLiteral("^"), QStringLiteral("$"), QStringLiteral("\\d+"), QStringLiteral("?"), QStringLiteral("+"),
            QStringLiteral("{2}"), QStringLiteral("bunny"), QStringLiteral("BUNNY"), QStringLiteral("Mbps"),
            QStringLiteral("%"), QStringLiteral("%1"), QStringLiteral("<b>"), QStringLiteral("  "),
            QStringLiteral("ünïcødé 🐰"), QStringLiteral("​"), QString(QChar(0)), QString(100000, QLatin1Char('x')),
            QString(5000, QLatin1Char('(')),
        };
        for (const QString& pattern : patterns) {
            view.searchEdit()->setText(pattern);
            QVERIFY2(view.visibleRows() == expected(pattern), qPrintable(pattern.left(40)));
            QVERIFY(!view.grab().isNull());
        }
        QVERIFY(expected(QStringLiteral("(a|b)*")) == 1);
        QVERIFY(expected(QStringLiteral(".*")) == 0);

        // Everything filtered out: the details ask for a selection, and opening does nothing.
        QSignalSpy open(&view, &LibraryView::openRequested);
        view.searchEdit()->setText(QStringLiteral("no run is called this"));
        QCOMPARE(view.visibleRows(), 0);
        view.selectBundle(bbb());  // filtered out: ignored
        QPushButton* openButton = nullptr;
        for (QPushButton* b : view.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Open Run"))
                openButton = b;
        QVERIFY(openButton);
        QTest::mouseClick(openButton, Qt::LeftButton);
        QCOMPARE(open.size(), 0);
        view.searchEdit()->clear();
        QCOMPARE(view.visibleRows(), 3);
    }

    void libraryOpensOnceAndNeverOpensBrokenRuns()
    {
        LibraryView view;
        view.resize(1000, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(view.isShowingWelcome());
        const QString broken = QStringLiteral("/nonexistent/broken");
        view.setBundles({summary(bbb()), fakeSummary(broken, QStringLiteral("ZZZ Broken"), 1, {QStringLiteral("<b>bad</b> & worse")})},
                        QStringLiteral("/some/where"));
        QVERIFY(!view.isShowingWelcome());
        QVERIFY(view.detailsText().contains(QStringLiteral("Big Buck Bunny")));

        QPushButton* openButton = nullptr;
        for (QPushButton* b : view.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Open Run"))
                openButton = b;
        QVERIFY(openButton);
        QSignalSpy open(&view, &LibraryView::openRequested);
        QTest::mouseClick(openButton, Qt::LeftButton);
        QCOMPARE(open.size(), 1);
        QCOMPARE(open.first().at(0).toString(), QDir(bbb()).absolutePath());

        auto* table = view.findChild<QTableView*>();
        QVERIFY(table);
        table->setFocus();
        QTest::keyClick(table, Qt::Key_Return);
        QCOMPARE(open.size(), 2);

        // The broken run: the errors are escaped into the details, and it cannot be opened.
        view.selectBundle(broken);
        QVERIFY(view.detailsText().contains(QStringLiteral("&lt;b&gt;bad&lt;/b&gt; &amp; worse")));
        QVERIFY(!openButton->isEnabled());
        QTest::mouseClick(openButton, Qt::LeftButton);
        QCOMPARE(open.size(), 2);
        // Down past the last row stays on it.
        QTest::keyClick(table, Qt::Key_Down);
        QTest::keyClick(table, Qt::Key_End);
        QVERIFY(view.detailsText().contains(QStringLiteral("ZZZ Broken")));

        // Replacing the bundles with none empties the table and the details.
        view.setBundles({}, QString());
        QCOMPARE(view.visibleRows(), 0);
        QTest::keyClick(table, Qt::Key_Return);
        QCOMPARE(open.size(), 2);
        view.setOpenBundle(QString());
        view.setOpenBundle(bbb());
        view.setScanning(QStringLiteral("/ünïcødé"));
        QVERIFY(!view.isShowingWelcome());
    }

    void welcomeScreenRowsEmitOnce()
    {
        LibraryView view;
        view.resize(1000, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QSignalSpy folder(&view, &LibraryView::openFolderRequested);
        QSignalSpy bundle(&view, &LibraryView::openBundleRequested);
        QSignalSpy recent(&view, &LibraryView::recentFolderRequested);

        const auto rows = [&view] {
            QList<QPushButton*> list;
            for (QPushButton* b : view.findChildren<QPushButton*>(QStringLiteral("actionRow")))
                if (b->isVisibleTo(&view))
                    list.append(b);
            return list;
        };
        QCOMPARE(rows().size(), 2);
        QTest::mouseClick(rows()[0], Qt::LeftButton);
        QTest::mouseClick(rows()[1], Qt::LeftButton);
        QCOMPARE(folder.size(), 1);
        QCOMPARE(bundle.size(), 1);
        QCOMPARE(recent.size(), 0);

        const QString odd = QStringLiteral("/tmp/ünï cødé/(a|b)*");
        view.setRecentFolders({QStringLiteral("/tmp/one"), odd, QString(), QStringLiteral("/")});
        QTRY_COMPARE(rows().size(), 6);
        QTest::mouseClick(rows()[3], Qt::LeftButton);
        QCOMPARE(recent.size(), 1);
        QCOMPARE(recent.first().at(0).toString(), odd);

        // Replacing the list many times leaves only the last one.
        for (int i = 0; i < 50; ++i)
            view.setRecentFolders({QStringLiteral("/tmp/%1").arg(i)});
        QTRY_COMPARE(rows().size(), 3);
        view.setRecentFolders({});
        QTRY_COMPARE(rows().size(), 2);
    }

    void welcomeScreenRecentListReplacedFromItsOwnClick()
    {
        // A receiver that updates the recent list as soon as a folder is chosen (as the window
        // does once a scan finishes) replaces the very row being clicked.
        LibraryView view;
        view.resize(1000, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setRecentFolders({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")});
        int calls = 0;
        connect(&view, &LibraryView::recentFolderRequested, &view, [&](const QString& folder) {
            ++calls;
            view.setRecentFolders({folder, QStringLiteral("/tmp/c")});
        });
        const auto recentRow = [&view]() -> QPushButton* {
            for (QPushButton* b : view.findChildren<QPushButton*>(QStringLiteral("actionRow")))
                if (b->isVisibleTo(&view)
                    && std::ranges::any_of(b->findChildren<QLabel*>(), [](QLabel* l) {
                           return l->text().startsWith(QDir::toNativeSeparators(QStringLiteral("/tmp/")));
                       }))
                    return b;
            return nullptr;
        };
        for (int i = 0; i < 5; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(recentRow(), kTimeoutMs);  // new rows are shown from the event loop
            QTest::mouseClick(recentRow(), Qt::LeftButton);
            QCOMPARE(calls, i + 1);
        }
        QCOMPARE(calls, 5);
    }

    // --- Notice banner ---------------------------------------------------------------------------

    void noticeBannerShowsReplacesAndExpires()
    {
        MainWindow window;
        QCOMPARE(window.noticeText(), QString());
        window.showNotice(QStringLiteral("kept"), 0);
        QCOMPARE(window.noticeText(), QStringLiteral("kept"));
        window.showNotice(QStringLiteral("also kept"), -5);
        QCOMPARE(window.noticeText(), QStringLiteral("also kept"));

        // A short notice goes away by itself.
        window.showNotice(QStringLiteral("brief"), 30);
        QCOMPARE(window.noticeText(), QStringLiteral("brief"));
        QTRY_COMPARE_WITH_TIMEOUT(window.noticeText(), QString(), kTimeoutMs);

        // A kept notice replacing a timed one is not hidden by the old timer.
        window.showNotice(QStringLiteral("timed"), 30);
        window.showNotice(QStringLiteral("stays"), 0);
        QTest::qWait(120);
        QCOMPARE(window.noticeText(), QStringLiteral("stays"));

        // Empty text hides it at once, whatever the time.
        window.showNotice(QString(), 10000);
        QCOMPARE(window.noticeText(), QString());

        // Long, unicode and markup-looking text.
        const QString longText = QString(20000, QLatin1Char('w')) + QStringLiteral(" 🛰️ <b>not bold</b> %1");
        window.showNotice(longText, 0);
        QCOMPARE(window.noticeText(), longText);
        window.resize(400, 300);
        QVERIFY(!window.grab().isNull());
        window.resize(1, 1);
        QVERIFY(!window.grab().isNull());
        window.shutdown();
    }

    // --- Settings dialog -------------------------------------------------------------------------

    void settingsDialogSignalsOnlyUserChanges()
    {
        QTemporaryDir cache;
        const QString path = cache.filePath(QStringLiteral("ünï cødé/renders"));
        SettingsDialog dialog(path);
        QSignalSpy engine(&dialog, &SettingsDialog::engineChosen);
        QSignalSpy overlay(&dialog, &SettingsDialog::overlayToggled);

        dialog.setEngine(Engine::Prerendered);
        dialog.setEngine(Engine::GStreamer);
        dialog.setEngine(static_cast<Engine>(42));  // not an engine: ignored
        dialog.setOverlayVisible(true);
        dialog.setOverlayVisible(true);
        dialog.setOverlayVisible(false);
        QCOMPARE(engine.size(), 0);
        QCOMPARE(overlay.size(), 0);
        // Setting the cache path does not create it; only "Show in Finder" would.
        QVERIFY(!QDir(path).exists());
        bool shown = false;
        for (QLabel* label : dialog.findChildren<QLabel*>())
            shown = shown || label->text() == QDir::toNativeSeparators(path);
        QVERIFY(shown);

        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QRadioButton* prerendered = nullptr;
        QRadioButton* gstreamer = nullptr;
        for (QRadioButton* b : dialog.findChildren<QRadioButton*>())
            (b->text() == engineName(Engine::Prerendered) ? prerendered : gstreamer) = b;
        QVERIFY(prerendered && gstreamer);
        QVERIFY(prerendered->isEnabled());
        // Click the indicator, at the left of the button.
        const QPoint indicator(6, prerendered->height() / 2);
        QTest::mouseClick(prerendered, Qt::LeftButton, {}, indicator);
        QCOMPARE(engine.size(), 1);
        QCOMPARE(engine.last().at(0).value<Engine>(), Engine::Prerendered);
        // Design choice: clicking the chosen engine again emits again (switching to the engine
        // in use is cheap for the window to ignore).
        QTest::mouseClick(prerendered, Qt::LeftButton, {}, indicator);
        QCOMPARE(engine.size(), 2);
        if (!gstreamer->isEnabled()) {
            QTest::mouseClick(gstreamer, Qt::LeftButton, {}, indicator);
            QCOMPARE(engine.size(), 2);
        }

        auto* box = dialog.findChild<QCheckBox*>();
        QVERIFY(box);
        QTest::mouseClick(box, Qt::LeftButton, {}, QPoint(6, box->height() / 2));
        QCOMPARE(overlay.size(), 1);
        QCOMPARE(overlay.last().at(0).toBool(), true);
        QTest::keyClick(box, Qt::Key_Space);
        QCOMPARE(overlay.size(), 2);
        QCOMPARE(overlay.last().at(0).toBool(), false);
        // The dialog has a fixed size: resizing is refused.
        const QSize size = dialog.size();
        dialog.resize(4000, 3000);
        QCOMPARE(dialog.size(), size);
    }

    // --- NetworkPathView animation and lifetime -------------------------------------------------

    void staticDiagramNeverAnimatesWhileHiddenAndDiesCleanly()
    {
        // The welcome screen's diagram, with no session or player. Whether it animates while
        // shown is a design choice of the view; hidden it must not, and destroying it while its
        // timer runs must be safe.
        auto view = std::make_unique<NetworkPathView>(nullptr, nullptr);
        QVERIFY(!animating(view.get()));
        QCOMPARE(view->minimumSizeHint().height(), view->sizeHint().height());
        view->resize(view->sizeHint());
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        QVERIFY(!view->grab().isNull());
        view->hide();
        QVERIFY(!animating(view.get()));
        for (int i = 0; i < 20; ++i) {
            view->setVisible(i % 2 == 0);
            if (i % 2)
                QVERIFY(!animating(view.get()));
        }
        view->show();
        view.reset();
        for (int i = 0; i < 20; ++i) {
            auto v = std::make_unique<NetworkPathView>(nullptr, nullptr);
            v->show();
            QTest::qWait(i % 3 == 0 ? 40 : 0);
        }
        QTest::qWait(100);
    }

    void sessionDiagramAnimatesOnlyWhileDownloadingAndRunning()
    {
        Rig rig;
        rig.session.setRun(downloadingRun());
        rig.path->resize(900, rig.path->sizeHint().height());
        rig.path->show();
        QVERIFY(QTest::qWaitForWindowExposed(rig.path.get()));
        // A segment is in flight, but the player is not running: no animation.
        QVERIFY(rig.path->isDownloading());
        QVERIFY(!animating(rig.path.get()));
        rig.session.clear();
        QVERIFY(!animating(rig.path.get()));
        QVERIFY(!rig.path->isDownloading());
    }

    void pathAnimationStopsWhenTheRunIsCleared()
    {
        // The GStreamer engine plays the synthetic run's segment files directly, without a render.
        if (!ReplaySourceFactory::isAvailable(Engine::GStreamer))
            QSKIP("built without GStreamer");
        QTemporaryDir cache;
        RenderQueue queue(RenderQueue::Options{cache.path(), QString(), 1});
        Session session;
        ReplayPlayer player(&session, &queue);
        player.resize(640, 360);
        player.show();
        QVERIFY(player.setEngine(Engine::GStreamer));
        const auto run = downloadingRun();
        session.setRun(run);

        auto view = std::make_unique<NetworkPathView>(&session, &player);
        view->resize(900, view->sizeHint().height());
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        QVERIFY(view->isDownloading());
        QVERIFY(!animating(view.get()));  // not running yet

        player.prepare();
        QTRY_VERIFY2_WITH_TIMEOUT(player.engine()->isReady() || player.engine()->state() == ReplaySource::State::Error,
                                  "engine never became ready", kTimeoutMs);
        if (player.engine()->state() == ReplaySource::State::Error)
            QSKIP("GStreamer cannot play the test media here");
        session.cursor()->setTime(run->playbackStart + 0.5, TimeCursor::Source::Timeline);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.isRunning(), kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(animating(view.get()), kTimeoutMs);
        QVERIFY(!view->grab().isNull());

        // Hidden: no animation.
        view->hide();
        QVERIFY(!animating(view.get()));
        view->show();

        // Destroyed while the animation runs: nothing fires into the dead view.
        QTRY_VERIFY_WITH_TIMEOUT(animating(view.get()) || !player.isRunning(), kTimeoutMs);
        view.reset();
        QTest::qWait(100);
        view = std::make_unique<NetworkPathView>(&session, &player);
        view->resize(900, view->sizeHint().height());
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));

        // Clearing the run stops the dashes at once and leaves the plain diagram.
        session.clear();
        QVERIFY(!animating(view.get()));
        QVERIFY(!view->isDownloading());
        QVERIFY(view->activityText().isEmpty());
        QCOMPARE(view->labels().last(), QStringLiteral("UE"));
        QTest::qWait(100);
        QVERIFY(!animating(view.get()));
        view.reset();
        player.releaseEngine();
    }

    void pathViewOutlivesItsSession()
    {
        // The view keeps its own reference to the run and only reads the session in response
        // to its signals, so a session destroyed first leaves a working, static view.
        auto session = std::make_unique<Session>();
        session->setRun(test::syntheticRun());
        NetworkPathView view(session.get(), nullptr);
        view.resize(900, 150);
        const QStringList labels = view.labels();
        session.reset();
        QCOMPARE(view.labels(), labels);
        QVERIFY(!view.grab().isNull());
        view.resize(10, 10);
        QVERIFY(!view.grab().isNull());
    }

    void rapidCreateAndDestroy()
    {
        QTemporaryDir cache;
        RenderQueue queue(RenderQueue::Options{cache.path(), QString(), 1});
        Session session;
        ReplayPlayer player(&session, &queue);
        const auto run = load(bbb());
        const long baseline = run.use_count();
        for (int i = 0; i < 60; ++i) {
            session.setRun(i % 3 == 0 ? nullptr : run);
            {
                NetworkPathView path(&session, &player);
                MomentPanel moment(&session, &player);
                auto* timeline = new SegmentTimeline(&session);
                TransportBar transport(&session, &player, timeline, TransportBar::Actions{});
                ReportView report(&session);
                session.cursor()->setTime(static_cast<double>(i));
                if (i % 7 == 0) {
                    QVERIFY(!path.grab().isNull());
                    QVERIFY(!transport.grab().isNull());
                }
                if (i % 5 == 0)
                    session.setRun(run);
            }
            auto* sidebar = new SourceList;
            auto* library = new LibraryView;
            auto* views = new SegmentedControl;
            views->addSegment(QStringLiteral("x"));
            delete views;
            delete library;
            delete sidebar;
        }
        session.clear();
        QCOMPARE(run.use_count(), baseline);
        player.releaseEngine();
    }
};

QTEST_MAIN(TestWidgets)
#include "tst_widgets.moc"
