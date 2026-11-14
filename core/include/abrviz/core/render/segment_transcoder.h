#pragma once

#include <QString>
#include <QStringList>

#include <stop_token>

namespace abrviz {

struct TranscodeJob {
    QString program;  ///< `ffmpeg`, or a path to it
    QStringList arguments;
};

struct TranscodeResult {
    bool ok = false;
    bool stopped = false;
    QString error;
};

/// Runs one `ffmpeg` command to completion on the calling thread. The process is held by a
/// `ProcessGuard` and polled every 100 ms, so a stop request kills it within 100 ms and no
/// process is left behind on any return path.
class SegmentTranscoder {
public:
    [[nodiscard]] static TranscodeResult transcode(const TranscodeJob& job, std::stop_token stop);
};

} // namespace abrviz
