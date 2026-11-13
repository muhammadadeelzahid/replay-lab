#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace abrviz::tools {

/// A path is a bundle if it contains `run.json`. Other folders are searched one level down.
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
