#include "abrviz/media/engine_support.h"

#include <QTest>

class TestMediaEngineSupport : public QObject {
    Q_OBJECT

private slots:
    void matchesBuildFlag()
    {
        const auto support = abrviz::media::engineSupport();
#if ABRVIZ_WITH_GSTREAMER
        QVERIFY(support.gstreamer);
        QVERIFY2(support.gstreamerVersion.startsWith(QStringLiteral("GStreamer 1.")),
                 qPrintable(support.gstreamerVersion));
#else
        QVERIFY(!support.gstreamer);
        QVERIFY(support.gstreamerVersion.isEmpty());
#endif
    }
};

QTEST_GUILESS_MAIN(TestMediaEngineSupport)
#include "tst_media_engine_support.moc"
