#pragma once

/**
 * @file
 * @brief Reports which replay engines this build contains.
 */

#include <QString>

namespace abrviz::media {

/**
 * @brief Which replay engines this build contains.
 *
 * See docs/architecture.md, "Choosing the engine".
 */
struct EngineSupport {
    bool gstreamer = false;     ///< built with ABR_WITH_GSTREAMER=ON
    QString gstreamerVersion;   ///< e.g. "GStreamer 1.28.9"; empty without GStreamer
};

/**
 * @brief Describes the replay engines compiled into this build.
 *
 * @return `gstreamer` is true and `gstreamerVersion` holds the version string of the linked
 *         GStreamer library when the build has GStreamer; otherwise both are left empty.
 */
[[nodiscard]] EngineSupport engineSupport();

} // namespace abrviz::media
