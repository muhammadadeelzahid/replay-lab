#include "abrviz/core/version.h"

#include <QRegularExpression>
#include <QTest>

class TestCoreVersion : public QObject {
    Q_OBJECT

private slots:
    void versionIsSemantic()
    {
        const QRegularExpression semver(QStringLiteral(R"(^\d+\.\d+\.\d+$)"));
        QVERIFY2(semver.match(abrviz::versionString()).hasMatch(),
                 qPrintable(abrviz::versionString()));
    }
};

QTEST_GUILESS_MAIN(TestCoreVersion)
#include "tst_core_version.moc"
