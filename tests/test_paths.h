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

/// `viewer_bundles_phasef/` in the source tree: the LLM, RobustMPC, MPC and BOLA runs. Tests
/// that need it skip when it is missing.
inline QString comparisonBundlesPath()
{
    return QStringLiteral(ABRVIZ_COMPARISON_BUNDLES_DIR);
}

inline bool haveComparisonBundles()
{
    return QDir(comparisonBundlesPath()).exists();
}

} // namespace abrviz::test
