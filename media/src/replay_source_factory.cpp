#include "abrviz/media/replay_source_factory.h"

#include "abrviz/media/prerendered_source.h"

#if ABRVIZ_WITH_GSTREAMER
#include "gst/gst_segment_source.h"
#endif

namespace abrviz {

QString engineName(Engine engine)
{
    switch (engine) {
    case Engine::GStreamer: return QStringLiteral("GStreamer");
    case Engine::Prerendered: return QStringLiteral("Pre-rendered");
    }
    return {};
}

QString engineKey(Engine engine)
{
    switch (engine) {
    case Engine::GStreamer: return QStringLiteral("gstreamer");
    case Engine::Prerendered: return QStringLiteral("prerendered");
    }
    return {};
}

std::optional<Engine> engineFromKey(const QString& key)
{
    for (const Engine engine : {Engine::GStreamer, Engine::Prerendered}) {
        if (key.compare(engineKey(engine), Qt::CaseInsensitive) == 0)
            return engine;
    }
    return std::nullopt;
}

QList<Engine> ReplaySourceFactory::available()
{
#if ABRVIZ_WITH_GSTREAMER
    return {Engine::GStreamer, Engine::Prerendered};
#else
    return {Engine::Prerendered};
#endif
}

bool ReplaySourceFactory::isAvailable(Engine engine)
{
    return available().contains(engine);
}

Engine ReplaySourceFactory::defaultEngine()
{
    return isAvailable(Engine::GStreamer) ? Engine::GStreamer : Engine::Prerendered;
}

std::unique_ptr<ReplaySource> ReplaySourceFactory::create(Engine engine, RenderQueue* queue)
{
    switch (engine) {
    case Engine::Prerendered:
        return std::make_unique<PrerenderedSource>(queue);
    case Engine::GStreamer:
#if ABRVIZ_WITH_GSTREAMER
        return std::make_unique<GstSegmentSource>();
#else
        return nullptr;
#endif
    }
    return nullptr;
}

} // namespace abrviz
