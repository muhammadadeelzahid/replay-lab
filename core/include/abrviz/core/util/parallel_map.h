#pragma once

#include <QFuture>
#include <QList>
#include <QPromise>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <concepts>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace abrviz {

/// Applies `fn` to every input on `pool`, one task per input, and returns the results in input
/// order. The future reports progress and can be cancelled (no result then).
///
/// Each task writes only its own output slot; an atomic countdown (acq_rel) lets the last task
/// to finish see every slot and publish the list, so no thread waits for another. This uses
/// only standard synchronisation, which ThreadSanitizer can check.
template <typename Input, typename Fn>
    requires std::invocable<Fn&, const Input&>
[[nodiscard]] auto parallelMap(QList<Input> inputs, Fn fn, QThreadPool* pool = nullptr)
    -> QFuture<QList<std::invoke_result_t<Fn&, const Input&>>>
{
    using Result = std::invoke_result_t<Fn&, const Input&>;

    struct State {
        QPromise<QList<Result>> promise;
        QList<Input> inputs;
        std::vector<std::optional<Result>> outputs;
        std::atomic<bool> ready{false};
        std::atomic<qsizetype> remaining{0};
        std::mutex progressMutex;
        int progress = 0;
    };

    auto state = std::make_shared<State>();
    state->inputs = std::move(inputs);
    state->outputs.resize(static_cast<std::size_t>(state->inputs.size()));
    state->promise.start();
    state->promise.setProgressRange(0, static_cast<int>(state->inputs.size()));
    QFuture<QList<Result>> future = state->promise.future();

    if (state->inputs.isEmpty()) {
        state->promise.addResult(QList<Result>{});
        state->promise.finish();
        return future;
    }

    state->remaining.store(state->inputs.size(), std::memory_order_relaxed);
    state->ready.store(true, std::memory_order_release);
    if (!pool)
        pool = QThreadPool::globalInstance();

    auto fnShared = std::make_shared<Fn>(std::move(fn));
    for (qsizetype i = 0; i < state->inputs.size(); ++i) {
        [[maybe_unused]] auto task = QtConcurrent::run(pool, [state, fnShared, i] {
            [[maybe_unused]] const bool ready = state->ready.load(std::memory_order_acquire);
            if (!state->promise.isCanceled()) {
                state->outputs[static_cast<std::size_t>(i)] =
                    (*fnShared)(state->inputs[i]);
                const std::scoped_lock lock(state->progressMutex);
                state->promise.setProgressValue(++state->progress);
            }
            if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) != 1)
                return;
            // Last task: every slot is written.
            if (!state->promise.isCanceled()) {
                QList<Result> results;
                results.reserve(state->inputs.size());
                for (auto& slot : state->outputs)
                    results.append(std::move(*slot));
                state->promise.addResult(std::move(results));
            }
            state->promise.finish();
        });
    }
    return future;
}

} // namespace abrviz
