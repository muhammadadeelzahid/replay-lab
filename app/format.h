#pragma once

#include "abrviz/core/model/run_data.h"

#include <QString>

namespace abrviz::format {

inline QString bitrate(double bitsPerSecond)
{
    if (bitsPerSecond >= 1e6)
        return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1e6, 0, 'f', bitsPerSecond >= 1e7 ? 0 : 1);
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1e3, 0, 'f', 0);
}

inline QString seconds(double value, int decimals = 2)
{
    return QStringLiteral("%1 s").arg(value, 0, 'f', decimals);
}

inline QString resolution(const Rung& rung)
{
    return QStringLiteral("%1p").arg(rung.height);
}

inline QString rungLabel(const VideoInfo& video, int rung)
{
    const Rung* r = video.rung(rung);
    if (!r)
        return QStringLiteral("rung %1").arg(rung);
    return QStringLiteral("%1, %2").arg(resolution(*r), bitrate(static_cast<double>(r->bitrate)));
}

} // namespace abrviz::format
