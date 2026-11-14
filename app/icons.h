#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>

namespace abrviz {

/// The line icons used in the toolbar, the sidebar and the transport bar.
enum class Glyph {
    Library,
    Replay,
    Analysis,
    Logs,
    Report,
    OpenFolder,
    OpenBundle,
    Play,
    Pause,
    StepBack,
    StepForward,
    PreviousSegment,
    NextSegment,
    Render,
    Engine,
    Overlay,
    Film,
    Inspector,
    Sidebar,
    Settings,
    Search,
    Satellite,
    Relay,
    Server,
    Device,
};

/// `glyph` drawn in `color` (normally the palette's text colour, so it suits light and dark
/// themes), sharp at every size Qt asks for. `selected` (if valid) is used for selected items.
[[nodiscard]] QIcon glyphIcon(Glyph glyph, const QColor& color, const QColor& selected = QColor());

/// The accent colour for icons and highlights: Apple's system blue for the appearance.
/// (Qt's Highlight on macOS is the darker selection colour.)
[[nodiscard]] QColor accentColor(const QPalette& palette);

/// The application icon (a play symbol beamed down by a satellite, under a magnifying glass).
[[nodiscard]] QIcon appIcon();

} // namespace abrviz
