#include "main_window.h"

#include "abrviz/core/version.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QTest>

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void showsTitleAndVersion()
    {
        abrviz::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QCOMPARE(window.windowTitle(), QStringLiteral("abr-viz"));
        QVERIFY(window.statusBar()->currentMessage().contains(abrviz::versionString()));
    }

    void hasFileMenuWithQuit()
    {
        abrviz::MainWindow window;
        const auto menus = window.menuBar()->actions();
        QVERIFY(!menus.isEmpty());
        QCOMPARE(menus.first()->text(), QStringLiteral("&File"));
        QVERIFY(menus.first()->menu() != nullptr);
        QCOMPARE(menus.first()->menu()->actions().size(), 1);
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
