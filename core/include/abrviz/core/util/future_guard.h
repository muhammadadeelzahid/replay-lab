#pragma once

/**
 * @file
 * @brief An owner for a running `QFuture` that cancels and waits for it on destruction.
 */

#include <QFuture>

#include <optional>
#include <utility>

namespace abrviz {

/**
 * @brief Owns a running task: the destructor cancels it and waits until it has finished.
 *
 * The task therefore cannot outlive the object that started it. Move-only.
 *
 * Cancelling only stops tasks that check for it (a `QPromise`-based task that tests
 * `isCanceled()`); waiting is unconditional.
 *
 * @warning Never guard a task whose continuations need the thread that destroys the guard.
 *
 * @tparam T Result type of the future.
 */
template <typename T>
class FutureGuard {
public:
    /**
     * @brief Creates a guard that holds no task.
     */
    FutureGuard() = default;

    /**
     * @brief Guards `future`.
     *
     * @param future The running task.
     */
    explicit FutureGuard(QFuture<T> future)
        : future_(std::move(future))
    {
    }

    /**
     * @brief Takes over the task of `other`, which is left empty.
     *
     * @param other The guard to move from.
     */
    FutureGuard(FutureGuard&& other) noexcept
        : future_(std::exchange(other.future_, std::nullopt))
    {
    }

    /**
     * @brief Cancels and waits for the current task, then takes over the task of `other`.
     *
     * `other` is left empty. Self-assignment does nothing.
     *
     * @param other The guard to move from.
     * @return This guard.
     */
    FutureGuard& operator=(FutureGuard&& other) noexcept
    {
        if (this != &other) {
            stop();
            future_ = std::exchange(other.future_, std::nullopt);
        }
        return *this;
    }

    /** @brief Not copyable: a task has a single owner. */
    FutureGuard(const FutureGuard&) = delete;
    FutureGuard& operator=(const FutureGuard&) = delete;

    /**
     * @brief Cancels the task and waits until it has finished.
     */
    ~FutureGuard() { stop(); }

    /**
     * @brief Cancels and waits for the current task, then guards `future`.
     *
     * @param future The new task; a default future leaves the guard holding an empty future.
     */
    void reset(QFuture<T> future = {})
    {
        stop();
        future_ = std::move(future);
    }

    /**
     * @brief Asks the task to stop without waiting for it.
     *
     * The destructor (or `reset`) still waits.
     */
    void cancel()
    {
        if (future_)
            future_->cancel();
    }

    /**
     * @brief Stops guarding the task without cancelling it.
     *
     * Use this once the task has finished: cancelling a finished future discards its result.
     *
     * @return The future, or nothing if the guard held none.
     */
    [[nodiscard]] std::optional<QFuture<T>> release() { return std::exchange(future_, std::nullopt); }

    /**
     * @brief Tells whether the guarded task is running.
     *
     * @return False if there is no task or it has finished.
     */
    [[nodiscard]] bool isRunning() const { return future_ && future_->isRunning(); }

    /**
     * @brief Returns the guarded future.
     *
     * @return The future, or nothing if the guard holds none.
     */
    [[nodiscard]] const std::optional<QFuture<T>>& future() const { return future_; }

private:
    /**
     * @brief Cancels the task, waits for it and forgets it. Does nothing without a task.
     */
    void stop() noexcept
    {
        if (!future_)
            return;
        future_->cancel();
        future_->waitForFinished();
        future_.reset();
    }

    std::optional<QFuture<T>> future_;  ///< the guarded task; empty when the guard holds none
};

} // namespace abrviz
