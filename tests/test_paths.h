#pragma once

#include <QDir>
#include <QString>

namespace abrviz::test {

/// `tests/data` in the source tree.
inline QString dataPath(const QString& relative)
{
    return QDir(QStringLiteral(ABRVIZ_TEST_DATA_DIR)).filePath(relative);
}

/// `viewer_bundles/` in the source tree. Tests that need it skip when it is missing.
inline QString bundlesPath()
{
    return QStringLiteral(ABRVIZ_BUNDLES_DIR);
}

inline bool haveFullBundles()
{
    return QDir(bundlesPath()).exists();
}

/// The LLM, RobustMPC, MPC and BOLA runs: the folder in ABRVIZ_COMPARISON_BUNDLES_DIR if set,
/// else `replaylab-runs/runs` (where the README downloads them from Hugging Face), else
/// `viewer_bundles_phasef/`, in the source tree. Tests that need them skip when they are missing.
inline QString comparisonBundlesPath()
{
    if (const QString set = qEnvironmentVariable("ABRVIZ_COMPARISON_BUNDLES_DIR"); !set.isEmpty())
        return set;
    const QString downloaded = QDir(QStringLiteral(ABRVIZ_SOURCE_DIR)).filePath(QStringLiteral("replaylab-runs/runs"));
    if (QDir(downloaded).exists())
        return downloaded;
    return QStringLiteral(ABRVIZ_COMPARISON_BUNDLES_DIR);
}

inline bool haveComparisonBundles()
{
    return QDir(comparisonBundlesPath()).exists();
}

} // namespace abrviz::test
