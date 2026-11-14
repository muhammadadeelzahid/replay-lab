#include "background_mode.h"

#import <AppKit/AppKit.h>

namespace abrviz {

void runInBackground()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

} // namespace abrviz
