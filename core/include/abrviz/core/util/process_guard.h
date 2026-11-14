#pragma once

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <stop_token>

namespace abrviz {

/// Owns a child process: the destructor kills it if it is still running and waits for it to
/// exit, so no process outlives the guard. Move-only; a `QProcess` (a QObject) cannot be moved
/// itself, so it is held through a `std::unique_ptr`.
///
/// The process belongs to the thread that started it and is driven with the blocking
/// `waitFor...` calls, so that thread needs no event loop.
class ProcessGuard {
public:
    ProcessGuard() = default;
    ProcessGuard(ProcessGuard&& other) noexcept;
    ProcessGuard& operator=(ProcessGuard&& other) noexcept;
    ProcessGuard(const ProcessGuard&) = delete;
    ProcessGuard& operator=(const ProcessGuard&) = delete;
    ~ProcessGuard();

    /// Starts `program`. Returns false (and sets `error()`) if it could not be started.
    bool start(const QString& program, const QStringList& arguments);

    enum class Outcome { Finished, Failed, Stopped };

    /// Waits for the process, calling `shouldStop` every `pollMs`; when it returns true the
    /// process is killed. `Finished` means it exited normally with code 0.
    Outcome wait(const std::function<bool()>& shouldStop, int pollMs = 100);
    Outcome wait(std::stop_token stop, int pollMs = 100)
    {
        return wait([&stop] { return stop.stop_requested(); }, pollMs);
    }

    /// Kills the process (if running) and waits for it to exit.
    void kill() noexcept;

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString error() const { return error_; }
    /// Everything the process wrote (stdout and stderr merged), read so far.
    [[nodiscard]] const QByteArray& output() const { return output_; }

    /// Processes started by any guard that have not exited yet (for tests and diagnostics).
    [[nodiscard]] static int liveCount();

private:
    void drain();

    std::unique_ptr<QProcess> process_;
    QString error_;
    QByteArray output_;
    bool counted_ = false;  // contributes to liveCount()
};

} // namespace abrviz
