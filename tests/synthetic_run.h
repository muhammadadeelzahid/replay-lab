#pragma once

#include "test_paths.h"

#include "abrviz/core/model/run_data.h"

#include <memory>

namespace abrviz::test {

/// A run with real media for render and replay tests: three 2.5 s segments of
/// tests/data/media/two_and_a_half_seconds.mp4 (30 fps), a stall of `stall` seconds after the
/// second, and playback cut off half way through the third. Replay time: 6.25 s + `stall`.
inline std::shared_ptr<const RunData> syntheticRun(double stall = 1.0, const QString& id = QStringLiteral("synthetic"))
{
    auto run = std::make_shared<RunData>();
    run->info.runId = id;
    run->info.playerSegmentDuration = 3.3;
    run->video.frameRate = 30.0;
    run->video.segmentCount = 3;
    run->video.ladder = {{0, 1'000'000, 64, 36}};
    const QString media = dataPath(QStringLiteral("media/two_and_a_half_seconds.mp4"));
    for (int i = 0; i < 3; ++i) {
        Segment segment;
        segment.index = i;
        segment.mediaPath = media;
        segment.mediaDuration = 2.5;
        run->segments.push_back(segment);
    }
    const double resume = 7.6 + stall;
    run->playback = {{0, 1.0, 4.267, 100, 1'000'000},
                     {1, 4.3, 7.567, 100, 1'000'000},
                     {2, resume, resume + 1.633, 50, 1'000'000}};
    run->stalls = {{7.6, resume, false}};
    run->streamStart = 0.5;
    run->playbackStart = 1.0;
    run->playbackEnd = resume + 1.666;
    for (double t = 1.0; t < run->playbackEnd; t += 0.033)
        run->buffer.push_back({t, t >= 7.6 && t < resume ? 0.0 : 2.0});
    return run;
}

} // namespace abrviz::test
