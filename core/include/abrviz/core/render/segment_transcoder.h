#pragma once

/**
 * @file
 * @brief Running one `ffmpeg` command with a stop token.
 */

#include <QString>
#include <QStringList>

#include <stop_token>

namespace abrviz {

/**
 * @brief One `ffmpeg` command line.
 */
struct TranscodeJob {
    QString program;  ///< `ffmpeg`, or a path to it
    QStringList arguments;  ///< command-line arguments
};

/**
 * @brief The outcome of one `ffmpeg` command.
 */
struct TranscodeResult {
    bool ok = false;       ///< exited normally with code 0
    bool stopped = false;  ///< killed, or not started, because a stop was requested
    QString error;         ///< set on failure; includes up to three lines of the output
};

/**
 * @brief Runs one `ffmpeg` command to completion on the calling thread.
 *
 * The process is held by a `ProcessGuard` and polled every 100 ms, so a stop request kills it
 * within 100 ms and no process is left behind on any return path.
 */
class SegmentTranscoder {
public:
    /**
     * @brief Runs the command and waits for it.
     *
     * @param job Program and arguments.
     * @param stop Stops the command when requested; checked before starting and while waiting.
     * @return The outcome: `ok`, `stopped`, or an error message.
     */
    [[nodiscard]] static TranscodeResult transcode(const TranscodeJob& job, std::stop_token stop);
};

} // namespace abrviz
