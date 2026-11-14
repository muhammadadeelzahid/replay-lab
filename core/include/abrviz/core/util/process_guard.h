#pragma once

/**
 * @file
 * @brief An owner for a child process that kills it on destruction.
 */

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <stop_token>

namespace abrviz {

/**
 * @brief Owns a child process: the destructor kills it if it is still running and waits for
 *        it to exit, so no process outlives the guard.
 *
 * Move-only; a `QProcess` (a QObject) cannot be moved itself, so it is held through a
 * `std::unique_ptr`.
 *
 * The process belongs to the thread that started it and is driven with the blocking
 * `waitFor...` calls, so that thread needs no event loop.
 */
class ProcessGuard {
public:
    /**
     * @brief Creates a guard with no process.
     */
    ProcessGuard() = default;

    /**
     * @brief Takes over the process, error and output of `other`.
     *
     * @param other The guard to move from; left without a process.
     */
    ProcessGuard(ProcessGuard&& other) noexcept;

    /**
     * @brief Kills the current process, then takes over the process, error and output of
     *        `other`.
     *
     * Self-assignment does nothing.
     *
     * @param other The guard to move from; left without a process.
     * @return This guard.
     */
    ProcessGuard& operator=(ProcessGuard&& other) noexcept;

    /** @brief Not copyable: a process has a single owner. */
    ProcessGuard(const ProcessGuard&) = delete;
    ProcessGuard& operator=(const ProcessGuard&) = delete;

    /**
     * @brief Kills the process if it is still running and waits for it to exit.
     */
    ~ProcessGuard();

    /**
     * @brief Starts `program`, with stdout and stderr merged.
     *
     * Kills any process the guard already held. Blocks until the process has started.
     *
     * @param program Program name or path.
     * @param arguments Command-line arguments.
     * @return False (and sets `error()`) if it could not be started.
     */
    bool start(const QString& program, const QStringList& arguments);

    /**
     * @brief How a wait ended.
     *
     * `Finished`: exited normally with code 0. `Failed`: crashed, exited with another code, or
     * no process was started. `Stopped`: killed because a stop was requested.
     */
    enum class Outcome { Finished, Failed, Stopped };

    /**
     * @brief Waits for the process, reading its output.
     *
     * Calls `shouldStop` every `pollMs`; when it returns true the process is killed. On
     * failure `error()` describes the exit.
     *
     * @param shouldStop Stop condition; may be empty.
     * @param pollMs Polling interval in milliseconds.
     * @return `Finished` if it exited normally with code 0, `Stopped` if it was killed,
     *         `Failed` otherwise.
     */
    Outcome wait(const std::function<bool()>& shouldStop, int pollMs = 100);

    /**
     * @brief Waits for the process, killing it when a stop is requested on `stop`.
     *
     * @param stop Stop token polled every `pollMs`.
     * @param pollMs Polling interval in milliseconds.
     * @return As for the other overload.
     */
    Outcome wait(std::stop_token stop, int pollMs = 100)
    {
        return wait([&stop] { return stop.stop_requested(); }, pollMs);
    }

    /**
     * @brief Kills the process (if running) and waits for it to exit.
     */
    void kill() noexcept;

    /**
     * @brief Tells whether a process is running.
     *
     * @return True if a process was started and has not exited.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns the reason the last start or wait failed.
     *
     * @return The message, or an empty string.
     */
    [[nodiscard]] QString error() const { return error_; }

    /**
     * @brief Returns everything the process wrote (stdout and stderr merged), read so far.
     *
     * Only the first 64 KiB are kept.
     *
     * @return The output.
     */
    [[nodiscard]] const QByteArray& output() const { return output_; }

    /**
     * @brief Returns the number of processes started by any guard that have not exited yet.
     *
     * For tests and diagnostics.
     *
     * @return The count.
     */
    [[nodiscard]] static int liveCount();

private:
    /**
     * @brief Appends the output available now to `output_`, up to the 64 KiB limit.
     */
    void drain();

    /** @brief The child process; null if none was started or it failed to start. */
    std::unique_ptr<QProcess> process_;
    QString error_;         ///< reason the last start or wait failed; empty if none
    QByteArray output_;     ///< merged stdout and stderr read so far, at most 64 KiB
    bool counted_ = false;  ///< the process is counted in liveCount()
};

} // namespace abrviz
