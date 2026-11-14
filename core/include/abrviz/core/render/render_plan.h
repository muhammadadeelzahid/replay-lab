#pragma once

/**
 * @file
 * @brief The plan for rendering a run's experienced video, and the `ffmpeg` commands for it.
 */

#include "abrviz/core/model/run_data.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace abrviz {

/**
 * @brief One played segment of the experienced video.
 *
 * `duration` seconds of its media file (less than the whole file when playback was cut off in
 * it), followed by `freezeAfter` seconds of its last frame for the stalls that came after it.
 */
struct RenderPart {
    int segment = 0;    ///< zero-based segment index
    QString mediaPath;  ///< media file of the segment at its played rung
    double duration = 0.0;     ///< seconds
    double freezeAfter = 0.0;  ///< seconds
};

/**
 * @brief How to build `experienced.mp4` for a run.
 *
 * It follows the replay timeline of `PlaybackClock` piece by piece, so the file's time is the
 * replay time.
 */
struct RenderPlan {
    std::vector<RenderPart> parts;  ///< played segments in replay order
    double frameRate = 30.0;        ///< output frames per second; the video's frame rate when known
    int width = 1920;  ///< output width in pixels; every part is scaled and padded to it
    int height = 1080;              ///< output height in pixels

    /**
     * @brief Returns the length of the output.
     *
     * @return The sum of every part's duration and freeze, in seconds.
     */
    [[nodiscard]] double duration() const;

    /**
     * @brief Identifies the output: the run id and a hash of everything that affects the file.
     *
     * The hash covers a render version (bumped when the encoding settings change), the size,
     * the frame rate and every part.
     *
     * @param runId The run id.
     * @return `<runId>-<12 hex digits of SHA-1>`.
     */
    [[nodiscard]] QString cacheKey(const QString& runId) const;
};

/**
 * @brief Builds the render plan of a run.
 *
 * Uses the video's frame rate if it is known, otherwise 30. Each stall is added as a freeze to
 * the part before it; a stall before the first part is dropped.
 *
 * @param run The loaded run.
 * @return The plan; no parts if nothing was played.
 */
[[nodiscard]] RenderPlan makeRenderPlan(const RunData& run);

/**
 * @brief Returns the `ffmpeg` arguments that turn one part into `output`.
 *
 * Trimmed, scaled and padded to the plan's size, at its frame rate, with the freeze appended,
 * H.264 without audio.
 *
 * @param plan The plan the part belongs to.
 * @param part The part to transcode.
 * @param output Path of the file to write.
 * @param threads `ffmpeg` thread count.
 * @return The arguments, without the program name.
 */
[[nodiscard]] QStringList transcodeArguments(const RenderPlan& plan, const RenderPart& part,
                                             const QString& output, int threads = 1);

/**
 * @brief Returns the `ffmpeg` arguments that join parts without re-encoding.
 *
 * @param listFile The concat demuxer's list file.
 * @param output Path of the file to write.
 * @return The arguments, without the program name.
 * @see concatList()
 */
[[nodiscard]] QStringList concatArguments(const QString& listFile, const QString& output);

/**
 * @brief Returns the concat demuxer's list file for `files`.
 *
 * @param files The part files, in order; single quotes in paths are escaped.
 * @return One `file 'PATH'` line per file, UTF-8.
 */
[[nodiscard]] QByteArray concatList(const QStringList& files);

} // namespace abrviz
