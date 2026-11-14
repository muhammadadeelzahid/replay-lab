#pragma once

#include <QString>

namespace abrviz::media {

/// Which replay engines this build contains (see docs/architecture.md, "Choosing the engine").
struct EngineSupport {
    bool gstreamer = false;     ///< built with ABR_WITH_GSTREAMER=ON
    QString gstreamerVersion;   ///< e.g. "GStreamer 1.28.9"; empty without GStreamer
};

[[nodiscard]] EngineSupport engineSupport();

} // namespace abrviz::media
