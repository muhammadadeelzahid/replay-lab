#pragma once

#include <QColor>

#include <algorithm>
#include <array>

namespace abrviz {

/// Colour of a rung, from dark (lowest) to bright (highest). The palette (viridis) stays
/// readable on light and dark backgrounds and for most colour-vision deficiencies.
inline QColor rungColor(int rung, int rungCount)
{
    static constexpr std::array<QRgb, 6> palette = {0xff440154, 0xff414487, 0xff2a788e,
                                                    0xff22a884, 0xff7ad151, 0xfffde725};
    if (rungCount <= 1)
        return QColor::fromRgb(palette[2]);
    const double position = std::clamp(static_cast<double>(rung) / (rungCount - 1), 0.0, 1.0);
    const double last = static_cast<double>(palette.size() - 1);
    const auto index = static_cast<std::size_t>(position * last + 0.5);
    return QColor::fromRgb(palette[index]);
}

/// A colour for text drawn on `background`.
inline QColor textColorOn(const QColor& background)
{
    return background.lightnessF() > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

} // namespace abrviz
