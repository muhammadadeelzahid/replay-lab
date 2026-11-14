#pragma once

namespace abrviz {

/// Makes the process a background-only application: no Dock icon, and it is never activated, so
/// it cannot take the keyboard focus from the application the user is working in. Used by
/// `--screenshot`, `--profile` and the tests that need the Cocoa platform; call it right after
/// creating the QApplication. Does nothing outside macOS.
void runInBackground();

} // namespace abrviz
