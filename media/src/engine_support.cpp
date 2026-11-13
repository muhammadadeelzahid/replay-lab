#include "abrviz/media/engine_support.h"

#if ABRVIZ_WITH_GSTREAMER
#include <gst/gst.h>

#include <memory>
#endif

namespace abrviz::media {

EngineSupport engineSupport()
{
    EngineSupport support;
#if ABRVIZ_WITH_GSTREAMER
    support.gstreamer = true;
    // gst_version_string() returns a newly allocated string that the caller must g_free().
    const std::unique_ptr<gchar, decltype(&g_free)> version(gst_version_string(), &g_free);
    support.gstreamerVersion = QString::fromUtf8(version.get());
#endif
    return support;
}

} // namespace abrviz::media
