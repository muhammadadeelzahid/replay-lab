#pragma once

/**
 * @file
 * @brief Expands command-line paths into run bundle folders for the tools.
 */

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace abrviz::tools {

/**
 * @brief Expands paths into the run bundles they name.
 *
 * A path is a bundle if it contains `run.json`. Other folders are searched one level down, in
 * name order.
 *
 * @param paths Bundle folders or folders that contain bundles.
 * @return The bundle folders found, in argument order; empty if none were found.
 */
inline QStringList expandBundlePaths(const QStringList& paths)
{
    QStringList bundles;
    for (const QString& path : paths) {
        const QDir dir(path);
        if (QFileInfo::exists(dir.filePath(QStringLiteral("run.json")))) {
            bundles.append(dir.path());
            continue;
        }
        const auto children = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& child : children) {
            if (QFileInfo::exists(dir.filePath(child + QStringLiteral("/run.json"))))
                bundles.append(dir.filePath(child));
        }
    }
    return bundles;
}

} // namespace abrviz::tools
