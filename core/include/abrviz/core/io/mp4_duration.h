#pragma once

/**
 * @file
 * @brief Reading the video duration from an MP4 file's headers.
 */

#include <QString>

#include <optional>

namespace abrviz {

/**
 * @brief Returns the duration of an MP4 file's video.
 *
 * Taken from the video track's media header (`moov/trak/mdia/mdhd`), or from the movie header
 * (`moov/mvhd`) if there is no video track. Reads only the box headers it needs.
 *
 * @param path Path of the MP4 file.
 * @return The duration in seconds, or nothing if the file cannot be read.
 */
[[nodiscard]] std::optional<double> readMp4Duration(const QString& path);

} // namespace abrviz
