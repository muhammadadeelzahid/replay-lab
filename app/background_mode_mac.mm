#include "background_mode.h"

#import <AppKit/AppKit.h>

namespace abrviz {

void runInBackground()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
}

void showBundleIconInDock()
{
    NSString* path = [[NSBundle mainBundle] pathForResource:@"AppIcon" ofType:@"icns"];
    if (!path)
        return;
    NSImage* icon = [[NSImage alloc] initWithContentsOfFile:path];
    if (icon) {
        [NSApp setApplicationIconImage:icon];
        [icon release];
    }
}

} // namespace abrviz
