#pragma once

/**
 * @file
 * @brief Text formatting helpers for bitrates, times, rungs and fonts shown in the panels.
 */

#include "abrviz/core/model/run_data.h"

#include <QFont>
#include <QString>

namespace abrviz::format {

/**
 * @brief Formats a bitrate as "N Mbps" or "N kbps".
 *
 * Rates of 1 Mbps and above are shown in Mbps, with one decimal below 10 Mbps and none above;
 * lower rates are shown in whole kbps.
 *
 * @param bitsPerSecond Bitrate in bits per second.
 * @return The formatted bitrate with its unit.
 */
inline QString bitrate(double bitsPerSecond)
{
    if (bitsPerSecond >= 1e6)
        return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1e6, 0, 'f', bitsPerSecond >= 1e7 ? 0 : 1);
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1e3, 0, 'f', 0);
}

/**
 * @brief Formats a duration or time as "N s".
 *
 * @param value Time in seconds.
 * @param decimals Number of decimals to show.
 * @return The formatted value with the "s" unit.
 */
inline QString seconds(double value, int decimals = 2)
{
    return QStringLiteral("%1 s").arg(value, 0, 'f', decimals);
}

/**
 * @brief Formats the resolution of a rung by its height, such as "720p".
 *
 * @param rung The ladder rung.
 * @return The rung height followed by "p".
 */
inline QString resolution(const Rung& rung)
{
    return QStringLiteral("%1p").arg(rung.height);
}

/**
 * @brief Labels a rung with its resolution and bitrate, such as "720p, 3.0 Mbps".
 *
 * @param video Video whose ladder holds the rung.
 * @param rung Index of the rung in the ladder.
 * @return The label, or "rung N" if the ladder has no such rung.
 */
inline QString rungLabel(const VideoInfo& video, int rung)
{
    const Rung* r = video.rung(rung);
    if (!r)
        return QStringLiteral("rung %1").arg(rung);
    return QStringLiteral("%1, %2").arg(resolution(*r), bitrate(static_cast<double>(r->bitrate)));
}

/**
 * @brief Scales a font, whether its size is in points or (from a style sheet) in pixels.
 *
 * @param font The font to scale (taken by value).
 * @param factor Scale factor applied to the size.
 * @return `font` scaled by `factor`; unchanged if it has neither a point nor a pixel size.
 */
inline QFont scaled(QFont font, qreal factor)
{
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * factor);
    else if (font.pixelSize() > 0)
        font.setPixelSize(qRound(font.pixelSize() * factor));
    return font;
}

} // namespace abrviz::format
