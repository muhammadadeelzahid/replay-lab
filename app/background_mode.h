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

} // namespace abrviz
