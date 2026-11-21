#pragma once

/**
 * @file
 * @brief Colours for ladder rungs and for text drawn on them.
 */

#include <QColor>

#include <algorithm>
#include <array>

namespace abrviz {

/**
 * @brief Colour of a rung, from dark (lowest) to bright (highest).
 *
 * The palette (viridis, six stops) stays readable on light and dark backgrounds and for most
 * colour-vision deficiencies. The rung's position in the ladder picks the nearest stop.
 *
 * @param rung Index of the rung, 0 for the lowest.
 * @param rungCount Number of rungs in the ladder.
 * @return The rung's colour; the middle palette colour if the ladder has one rung or none.
 */
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

/// @return The colour of stalls, the same in the timeline, the rung strips and the colour guide.
inline QColor stallColor()
{
    return {0xd6, 0x27, 0x28};
}

/// @return The colour of buffer-guard overrides of the LLM's choice in the timeline.
inline QColor overrideColor()
{
    return {0xe6, 0x55, 0x0d};
}

/**
 * @brief A colour for text drawn on a background.
 *
 * @param background The background colour.
 * @return Black on light backgrounds (lightness above 0.55), white otherwise.
 */
inline QColor textColorOn(const QColor& background)
{
    return background.lightnessF() > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

} // namespace abrviz
