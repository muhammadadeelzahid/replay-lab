#pragma once

/**
 * @file
 * @brief Running the process as a background-only application on macOS.
 */

namespace abrviz {

/**
 * @brief Makes the process a background-only application.
 *
 * The application gets no Dock icon and is never activated, so it cannot take the keyboard
 * focus from the application the user is working in. Used by `--screenshot`, `--profile` and
 * the tests that need the Cocoa platform.
 *
 * @note Call it right after creating the QApplication. Does nothing outside macOS.
 */
void runInBackground();

/**
 * @brief Shows the bundle's current icon in the Dock.
 *
 * The Dock can keep showing an icon it cached for the bundle path before the icon changed;
 * this sets the running application's Dock icon from `AppIcon.icns` in the bundle, so it always
 * matches the icon the app shows inside its windows.
 *
 * @note Does nothing outside macOS, or when the executable is not in a bundle.
 */
void showBundleIconInDock();

} // namespace abrviz
