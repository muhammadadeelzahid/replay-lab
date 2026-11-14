#pragma once

#include "abrviz/core/model/run_data.h"

#include <QObject>
#include <QString>

#include <memory>

class QVideoSink;

namespace abrviz {

/// A replay engine: shows the run's played segments, at their played rungs, into a video sink.
/// Positions are replay times (see `PlaybackClock`). Every engine lives on the GUI thread and
/// reports through signals.
class ReplaySource : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Preparing, Playing, Paused, Stalled, Ended, Error };
    Q_ENUM(State)

    using QObject::QObject;
    ~ReplaySource() override = default;

    [[nodiscard]] virtual QString name() const = 0;
    virtual void setRun(std::shared_ptr<const RunData> run) = 0;
    virtual void setVideoSink(QVideoSink* sink) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double replayTime) = 0;

    /// Some engines need work before the run can be shown (rendering a file). `prepare` starts
    /// it; `isReady` is true once the engine can play.
    [[nodiscard]] virtual bool isReady() const = 0;
    virtual void prepare() {}

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] double position() const { return position_; }

signals:
    void positionChanged(double replayTime);
    void stateChanged(abrviz::ReplaySource::State state);
    void errorOccurred(const QString& message);
    /// Progress of `prepare`, from 0 to 1.
    void preparationProgress(double fraction);

protected:
    void setState(State state)
    {
        if (state == state_)
            return;
        state_ = state;
        emit stateChanged(state);
    }

    void setPosition(double replayTime)
    {
        position_ = replayTime;
        emit positionChanged(replayTime);
    }

private:
    State state_ = State::Idle;
    double position_ = 0.0;
};

} // namespace abrviz
