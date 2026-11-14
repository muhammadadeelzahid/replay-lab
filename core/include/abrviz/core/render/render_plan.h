#pragma once

#include "abrviz/core/model/run_data.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace abrviz {

/// One played segment of the experienced video: `duration` seconds of its media file (less than
/// the whole file when playback was cut off in it), followed by `freezeAfter` seconds of its last
/// frame for the stalls that came after it.
struct RenderPart {
    int segment = 0;
    QString mediaPath;
    double duration = 0.0;
    double freezeAfter = 0.0;
};

/// How to build `experienced.mp4` for a run. It follows the replay timeline of
/// `PlaybackClock` piece by piece, so the file's time is the replay time.
struct RenderPlan {
    std::vector<RenderPart> parts;
    double frameRate = 30.0;
    int width = 1920;
    int height = 1080;

    [[nodiscard]] double duration() const;
    /// Identifies the output: the run id and a hash of everything that affects the file.
    [[nodiscard]] QString cacheKey(const QString& runId) const;
};

[[nodiscard]] RenderPlan makeRenderPlan(const RunData& run);

/// `ffmpeg` arguments that turn one part into `output`: trimmed, scaled and padded to the
/// plan's size, at its frame rate, with the freeze appended, H.264 without audio.
[[nodiscard]] QStringList transcodeArguments(const RenderPlan& plan, const RenderPart& part,
                                             const QString& output, int threads = 1);

/// `ffmpeg` arguments that join the parts listed in `listFile` (concat demuxer) into `output`
/// without re-encoding.
[[nodiscard]] QStringList concatArguments(const QString& listFile, const QString& output);

/// The concat demuxer's list file for `files`.
[[nodiscard]] QByteArray concatList(const QStringList& files);

} // namespace abrviz
