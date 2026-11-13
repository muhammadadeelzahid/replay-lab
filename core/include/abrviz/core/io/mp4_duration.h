#pragma once

#include <QString>

#include <optional>

namespace abrviz {

/// Duration of an MP4 file's video in seconds, from the video track's media header
/// (`moov/trak/mdia/mdhd`), or from the movie header (`moov/mvhd`) if there is no video track.
/// Reads only the box headers it needs. Returns nothing if the file cannot be read.
[[nodiscard]] std::optional<double> readMp4Duration(const QString& path);

} // namespace abrviz
