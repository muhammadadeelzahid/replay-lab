#pragma once

/**
 * @file
 * @brief The `ReplaySource` interface that every replay engine implements.
 */

#include "abrviz/core/model/run_data.h"

#include <QObject>
#include <QString>

#include <memory>

class QVideoSink;

namespace abrviz {

/**
 * @brief A replay engine: shows the run's played segments, at their played rungs, into a video
 *        sink.
 *
 * Positions are replay times (see `PlaybackClock`). Every engine lives on the GUI thread and
 * reports through signals.
 */
class ReplaySource : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Playback state of an engine.
     *
     * `Idle`: no run, or a run that is not prepared yet. `Preparing`: getting ready to show the
     * run (rendering, loading, prerolling). `Paused`/`Playing`: ready, paused or playing.
     * `Stalled`: playing through a stall, the replay clock runs while the picture is held.
     * `Ended`: reached the end of the replay. `Error`: failed; `errorOccurred` said why.
     */
    enum class State { Idle, Preparing, Playing, Paused, Stalled, Ended, Error };
    Q_ENUM(State)

    /**
     * @brief Uses `QObject`'s constructors (an optional parent).
     */
    using QObject::QObject;

    /**
     * @brief Destroys the engine.
     */
    ~ReplaySource() override = default;

    /**
     * @brief The engine's name as shown to the user.
     *
     * @return The translated display name.
     */
    [[nodiscard]] virtual QString name() const = 0;

    /**
     * @brief Replaces the run to show, stopping whatever was playing.
     *
     * The state goes back to `Idle` (or on to `Preparing`) and the position to 0.
     *
     * @param run The run to show; null leaves the engine empty.
     */
    virtual void setRun(std::shared_ptr<const RunData> run) = 0;

    /**
     * @brief Sets the sink the engine shows its frames in.
     *
     * @param sink The video sink (not owned); may be null.
     */
    virtual void setVideoSink(QVideoSink* sink) = 0;

    /**
     * @brief Starts or resumes playback; deferred until the engine is ready.
     */
    virtual void play() = 0;

    /**
     * @brief Pauses playback.
     */
    virtual void pause() = 0;

    /**
     * @brief Moves to a replay time; deferred until the engine is ready.
     *
     * @param replayTime Target position on the replay timeline (seconds).
     */
    virtual void seek(double replayTime) = 0;

    /**
     * @brief Whether the engine can play the current run.
     *
     * Some engines need work before the run can be shown (rendering a file); `prepare()` starts
     * it.
     *
     * @return True once the engine can play.
     * @see prepare()
     */
    [[nodiscard]] virtual bool isReady() const = 0;

    /**
     * @brief Starts the work an engine needs before it can show the run.
     *
     * Does nothing by default. Progress is reported through `preparationProgress`.
     *
     * @see isReady()
     */
    virtual void prepare() {}

    /**
     * @brief The current playback state.
     *
     * @return The state last set by the engine.
     */
    [[nodiscard]] State state() const { return state_; }

    /**
     * @brief The current position.
     *
     * @return The last reported replay time (seconds).
     */
    [[nodiscard]] double position() const { return position_; }

signals:
    /**
     * @brief Reports the current replay time; emitted repeatedly while playing and after seeks.
     *
     * @param replayTime Position on the replay timeline (seconds).
     */
    void positionChanged(double replayTime);

    /**
     * @brief Emitted when the playback state changes (not when it is set to the same value).
     *
     * @param state The new state.
     */
    void stateChanged(abrviz::ReplaySource::State state);

    /**
     * @brief Emitted when the engine fails, after the state has changed to `Error`.
     *
     * @param message What went wrong, for the user.
     */
    void errorOccurred(const QString& message);

    /**
     * @brief Progress of `prepare()`.
     *
     * @param fraction Fraction done, from 0 to 1.
     */
    void preparationProgress(double fraction);

protected:
    /**
     * @brief Sets the state and emits `stateChanged` if it differs from the current one.
     *
     * @param state The new state.
     */
    void setState(State state)
    {
        if (state == state_)
            return;
        state_ = state;
        emit stateChanged(state);
    }

    /**
     * @brief Sets the position and always emits `positionChanged`.
     *
     * @param replayTime Position on the replay timeline (seconds).
     */
    void setPosition(double replayTime)
    {
        position_ = replayTime;
        emit positionChanged(replayTime);
    }

private:
    State state_ = State::Idle;  ///< current state, set by `setState()`
    double position_ = 0.0;  ///< replay time (seconds)
};

} // namespace abrviz
