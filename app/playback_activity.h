#pragma once

/**
 * @file
 * @brief An RAII token that tells the operating system video is playing.
 */

#include <memory>

namespace abrviz {

/**
 * @brief Tells the operating system that video is playing, for as long as the object lives.
 *
 * On macOS this keeps App Nap from slowing the app's timers when its window is hidden or
 * covered, which would make the replay stutter. Elsewhere it does nothing.
 */
class PlaybackActivity {
public:
    /**
     * @brief Begins the activity.
     *
     * On macOS this begins a user-initiated, latency-critical NSProcessInfo activity.
     */
    PlaybackActivity();

    /**
     * @brief Ends the activity begun by the constructor.
     */
    ~PlaybackActivity();

    /** @brief Not copyable or movable: the object is the activity. */
    PlaybackActivity(const PlaybackActivity&) = delete;
    PlaybackActivity& operator=(const PlaybackActivity&) = delete;
    PlaybackActivity(PlaybackActivity&&) = delete;
    PlaybackActivity& operator=(PlaybackActivity&&) = delete;

private:
    /**
     * @brief The platform's handle for the activity (empty outside macOS).
     */
    struct Token;
    std::unique_ptr<Token> token_;  ///< The platform handle; never null.
};

} // namespace abrviz
