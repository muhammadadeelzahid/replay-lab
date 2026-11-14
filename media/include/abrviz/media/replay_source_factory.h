#pragma once

#include "abrviz/media/replay_source.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace abrviz {

class RenderQueue;

enum class Engine { GStreamer, Prerendered };

[[nodiscard]] QString engineName(Engine engine);
/// "gstreamer" or "prerendered", as used by `--engine` and the settings.
[[nodiscard]] QString engineKey(Engine engine);
[[nodiscard]] std::optional<Engine> engineFromKey(const QString& key);

/// Creates replay engines. The GStreamer engine is only available in builds with
/// `ABR_WITH_GSTREAMER=ON`.
class ReplaySourceFactory {
public:
    [[nodiscard]] static QList<Engine> available();
    [[nodiscard]] static bool isAvailable(Engine engine);
    /// GStreamer when it is built in, otherwise the pre-rendered engine.
    [[nodiscard]] static Engine defaultEngine();
    /// Null if `engine` is not available. `queue` must outlive the engine. The engine has no
    /// Qt parent: the `unique_ptr` is its only owner.
    [[nodiscard]] static std::unique_ptr<ReplaySource> create(Engine engine, RenderQueue* queue);
};

} // namespace abrviz
