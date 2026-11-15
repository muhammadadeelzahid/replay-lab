#pragma once

/**
 * @file
 * @brief The app's look: Apple's neutral greys in light and dark mode, with a coral accent
 *        taken from the app icon.
 */

#include <QColor>
#include <QPalette>
#include <QString>

class QApplication;

namespace abrviz::theme {

/**
 * @brief The accent: buttons, the selected tab, selections and highlights.
 *
 * @param dark Whether it is for the dark appearance (a little lighter there).
 * @return The coral of the app icon's play symbol.
 */
[[nodiscard]] QColor accent(bool dark);

/**
 * @brief The palette for an appearance.
 *
 * @param dark True for dark mode, false for light mode.
 * @return Neutral greys for every role, with the accent as the highlight.
 */
[[nodiscard]] QPalette palette(bool dark);

/**
 * @brief The application style sheet for an appearance: fields, tables, tooltips and scroll
 *        bars in the palette's greys.
 *
 * @param dark True for dark mode, false for light mode.
 * @return The style sheet.
 */
[[nodiscard]] QString styleSheet(bool dark);

/**
 * @brief Applies the theme to the application and keeps it in step with the system appearance.
 *
 * Follows the system's light or dark mode, and switches the palette and the style sheet when
 * the user changes it. The environment variable `ABRVIZ_APPEARANCE` (`light` or `dark`) fixes
 * one appearance instead.
 *
 * @param app The application.
 */
void apply(QApplication& app);

} // namespace abrviz::theme
