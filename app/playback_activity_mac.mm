#include "playback_activity.h"

#import <Foundation/Foundation.h>

namespace abrviz {

/** @brief The NSProcessInfo activity held while the replay runs. */
struct PlaybackActivity::Token {
    id<NSObject> activity = nil;  ///< The retained NSProcessInfo activity.
};

PlaybackActivity::PlaybackActivity()
    : token_(std::make_unique<Token>())
{
    const NSActivityOptions options = NSActivityUserInitiated | NSActivityLatencyCritical;
    token_->activity = [[NSProcessInfo processInfo] beginActivityWithOptions:options
                                                                      reason:@"Playing a replay"];
    [token_->activity retain];
}

PlaybackActivity::~PlaybackActivity()
{
    if (token_->activity) {
        [[NSProcessInfo processInfo] endActivity:token_->activity];
        [token_->activity release];
    }
}

} // namespace abrviz
