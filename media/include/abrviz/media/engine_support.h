#pragma once

#include <QString>

namespace abrviz::media {

/// Which replay engines this build contains (see docs/ARCHITECTURE.md, section 7.1).
struct EngineSupport {
    bool gstreamer = false;     ///< built with ABR_WITH_GSTREAMER=ON
    QString gstreamerVersion;   ///< e.g. "GStreamer 1.28.9"; empty without GStreamer
};

[[nodiscard]] EngineSupport engineSupport();

} // namespace abrviz::media
