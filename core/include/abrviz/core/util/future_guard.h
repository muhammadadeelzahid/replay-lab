#pragma once

#include <QFuture>

#include <optional>
#include <utility>

namespace abrviz {

/// Owns a running task: the destructor cancels it and waits until it has finished, so the task
/// cannot outlive the object that started it. Move-only.
///
/// Cancelling only stops tasks that check for it (a `QPromise`-based task that tests
/// `isCanceled()`); waiting is unconditional. Never guard a task whose continuations need the
/// thread that destroys the guard.
template <typename T>
class FutureGuard {
public:
    FutureGuard() = default;
    explicit FutureGuard(QFuture<T> future)
        : future_(std::move(future))
    {
    }

    FutureGuard(FutureGuard&& other) noexcept
        : future_(std::exchange(other.future_, std::nullopt))
    {
    }

    FutureGuard& operator=(FutureGuard&& other) noexcept
    {
        if (this != &other) {
            stop();
            future_ = std::exchange(other.future_, std::nullopt);
        }
        return *this;
    }

    FutureGuard(const FutureGuard&) = delete;
    FutureGuard& operator=(const FutureGuard&) = delete;

    ~FutureGuard() { stop(); }

    /// Cancels and waits for the current task, then guards `future`.
    void reset(QFuture<T> future = {})
    {
        stop();
        future_ = std::move(future);
    }

    /// Asks the task to stop without waiting for it; the destructor (or `reset`) still waits.
    void cancel()
    {
        if (future_)
            future_->cancel();
    }

    /// Stops guarding the task without cancelling it. Use this once the task has finished:
    /// cancelling a finished future discards its result.
    [[nodiscard]] std::optional<QFuture<T>> release() { return std::exchange(future_, std::nullopt); }

    [[nodiscard]] bool isRunning() const { return future_ && future_->isRunning(); }
    [[nodiscard]] const std::optional<QFuture<T>>& future() const { return future_; }

private:
    void stop() noexcept
    {
        if (!future_)
            return;
        future_->cancel();
        future_->waitForFinished();
        future_.reset();
    }

    std::optional<QFuture<T>> future_;
};

} // namespace abrviz
