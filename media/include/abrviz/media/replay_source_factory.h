#pragma once

/**
 * @file
 * @brief The `Engine` choices and the factory that creates replay engines.
 */

#include "abrviz/media/replay_source.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

namespace abrviz {

class RenderQueue;

/**
 * @brief The replay engines a build can contain.
 */
enum class Engine { GStreamer, Prerendered };

/**
 * @brief The display name of an engine.
 *
 * @param engine The engine.
 * @return "GStreamer" or "Pre-rendered"; empty for an unknown value.
 */
[[nodiscard]] QString engineName(Engine engine);

/**
 * @brief The key of an engine, as used by `--engine` and the settings.
 *
 * @param engine The engine.
 * @return "gstreamer" or "prerendered"; empty for an unknown value.
 * @see engineFromKey()
 */
[[nodiscard]] QString engineKey(Engine engine);

/**
 * @brief Parses an engine key.
 *
 * @param key "gstreamer" or "prerendered", compared case-insensitively.
 * @return The engine, or `std::nullopt` if `key` names none. The engine may still be missing
 *         from this build; check `ReplaySourceFactory::isAvailable()`.
 * @see engineKey()
 */
[[nodiscard]] std::optional<Engine> engineFromKey(const QString& key);

/**
 * @brief Creates replay engines.
 *
 * The GStreamer engine is only available in builds with `ABR_WITH_GSTREAMER=ON`.
 */
class ReplaySourceFactory {
public:
    /**
     * @brief The engines this build contains.
     *
     * @return GStreamer (if built in) followed by the pre-rendered engine.
     */
    [[nodiscard]] static QList<Engine> available();

    /**
     * @brief Whether this build contains an engine.
     *
     * @param engine The engine to check.
     * @return True if `engine` is in `available()`.
     */
    [[nodiscard]] static bool isAvailable(Engine engine);

    /**
     * @brief The engine to use when none is chosen.
     *
     * @return GStreamer when it is built in, otherwise the pre-rendered engine.
     */
    [[nodiscard]] static Engine defaultEngine();

    /**
     * @brief Creates an engine.
     *
     * The engine has no Qt parent: the `unique_ptr` is its only owner.
     *
     * @param engine The engine to create.
     * @param queue The render queue used by the pre-rendered engine; must outlive the engine.
     * @return The new engine, or null if `engine` is not available in this build.
     */
    [[nodiscard]] static std::unique_ptr<ReplaySource> create(Engine engine, RenderQueue* queue);
};

} // namespace abrviz
