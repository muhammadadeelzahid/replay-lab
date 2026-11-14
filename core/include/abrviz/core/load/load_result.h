#pragma once

/**
 * @file
 * @brief The result of loading a bundle.
 */

#include "abrviz/core/model/run_data.h"

#include <QMetaType>
#include <QStringList>

#include <memory>

namespace abrviz {

/**
 * @brief The result of loading a bundle.
 *
 * `run` is set when the essential files could be read, even if other files had errors;
 * `errors` lists every problem found.
 */
struct LoadResult {
    /** @brief The loaded run; null if an essential file could not be read. */
    std::shared_ptr<const RunData> run;
    QStringList errors;  ///< every problem found, prefixed with file and line

    /**
     * @brief Tells whether the load succeeded without problems.
     *
     * @return True if `run` is set and `errors` is empty.
     */
    [[nodiscard]] bool ok() const { return run && errors.isEmpty(); }
};

} // namespace abrviz

Q_DECLARE_METATYPE(abrviz::LoadResult)
