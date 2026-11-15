#pragma once

/**
 * @file
 * @brief The app's look: a dark, cinematic theme in every appearance.
 */

#include <QColor>
#include <QPalette>

class QApplication;

namespace abrviz::theme {

/// The window's background: a deep navy black.
inline const QColor kBackground(0x0b, 0x0d, 0x14);
/// Raised surfaces: the sidebar, cards and panels.
inline const QColor kSurface(0x13, 0x17, 0x22);
/// Surfaces above those: fields, hovered rows, chips.
inline const QColor kRaised(0x1c, 0x22, 0x31);
/// The sidebar's background, darker than the window.
inline const QColor kSidebar(0x07, 0x09, 0x0f);
/// Main text.
inline const QColor kText(0xf4, 0xf6, 0xfb);
/// Secondary text.
inline const QColor kMuted(0x8d, 0x96, 0xab);
/// The accent: buttons, selection and highlights.
inline const QColor kAccent(0x2f, 0x7b, 0xff);

/**
 * @brief The palette of the theme.
 *
 * @return Dark colours for every role, with the accent as the highlight.
 */
[[nodiscard]] QPalette palette();

/**
 * @brief Applies the theme to the application: the dark colour scheme (so native controls and
 *        the title bar are dark too), the palette, and a style sheet for scroll bars, fields,
 *        tables and menus.
 *
 * @param app The application.
 */
void apply(QApplication& app);

} // namespace abrviz::theme
