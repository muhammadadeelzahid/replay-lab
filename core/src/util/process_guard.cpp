#include "abrviz/core/util/process_guard.h"

#include <atomic>
#include <utility>

namespace abrviz {

namespace {

std::atomic<int> gLive{0};

constexpr qint64 kMaxOutput = 64 * 1024;  // keep the start of the output for error messages

} // namespace

ProcessGuard::ProcessGuard(ProcessGuard&& other) noexcept
    : process_(std::move(other.process_))
    , error_(std::move(other.error_))
    , output_(std::move(other.output_))
    , counted_(std::exchange(other.counted_, false))
{
}

ProcessGuard& ProcessGuard::operator=(ProcessGuard&& other) noexcept
{
    if (this != &other) {
        kill();
        process_ = std::move(other.process_);
        error_ = std::move(other.error_);
        output_ = std::move(other.output_);
        counted_ = std::exchange(other.counted_, false);
    }
    return *this;
}

ProcessGuard::~ProcessGuard()
{
    kill();
}

bool ProcessGuard::start(const QString& program, const QStringList& arguments)
{
    kill();
    process_ = std::make_unique<QProcess>();
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->start(program, arguments, QIODevice::ReadOnly);
    if (!process_->waitForStarted()) {
        error_ = QStringLiteral("could not start %1: %2").arg(program, process_->errorString());
        process_.reset();
        return false;
    }
    counted_ = true;
    gLive.fetch_add(1, std::memory_order_relaxed);
    return true;
}

ProcessGuard::Outcome ProcessGuard::wait(const std::function<bool()>& shouldStop, int pollMs)
{
    if (!process_)
        return Outcome::Failed;
    while (!process_->waitForFinished(pollMs)) {
        drain();
        if (shouldStop && shouldStop()) {
            kill();
            return Outcome::Stopped;
        }
        if (process_->state() == QProcess::NotRunning)
            break;  // waitForFinished also returns false on an error
    }
    drain();
    const bool ok = process_->exitStatus() == QProcess::NormalExit && process_->exitCode() == 0;
    if (!ok) {
        error_ = process_->exitStatus() == QProcess::CrashExit
            ? QStringLiteral("the process crashed")
            : QStringLiteral("the process exited with code %1").arg(process_->exitCode());
    }
    if (counted_) {
        counted_ = false;
        gLive.fetch_sub(1, std::memory_order_relaxed);
    }
    return ok ? Outcome::Finished : Outcome::Failed;
}

void ProcessGuard::kill() noexcept
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(-1);
    }
    if (counted_) {
        counted_ = false;
        gLive.fetch_sub(1, std::memory_order_relaxed);
    }
}

bool ProcessGuard::isRunning() const
{
    return process_ && process_->state() != QProcess::NotRunning;
}

void ProcessGuard::drain()
{
    const QByteArray data = process_->readAll();
    if (output_.size() < kMaxOutput)
        output_ += data.left(kMaxOutput - output_.size());
}

int ProcessGuard::liveCount()
{
    return gLive.load(std::memory_order_relaxed);
}

} // namespace abrviz
