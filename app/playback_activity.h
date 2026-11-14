#pragma once

#include <memory>

namespace abrviz {

/// Tells the operating system that video is playing, for as long as the object lives. On macOS
/// this keeps App Nap from slowing the app's timers when its window is hidden or covered, which
/// would make the replay stutter. Elsewhere it does nothing.
class PlaybackActivity {
public:
    PlaybackActivity();
    ~PlaybackActivity();

    PlaybackActivity(const PlaybackActivity&) = delete;
    PlaybackActivity& operator=(const PlaybackActivity&) = delete;
    PlaybackActivity(PlaybackActivity&&) = delete;
    PlaybackActivity& operator=(PlaybackActivity&&) = delete;

private:
    struct Token;
    std::unique_ptr<Token> token_;
};

} // namespace abrviz
