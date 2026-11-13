#pragma once

#include "abrviz/core/model/run_data.h"

#include <QMetaType>
#include <QStringList>

#include <memory>

namespace abrviz {

/// The result of loading a bundle. `run` is set when the essential files could be read, even
/// if other files had errors; `errors` lists every problem found.
struct LoadResult {
    std::shared_ptr<const RunData> run;
    QStringList errors;

    [[nodiscard]] bool ok() const { return run && errors.isEmpty(); }
};

} // namespace abrviz

Q_DECLARE_METATYPE(abrviz::LoadResult)
