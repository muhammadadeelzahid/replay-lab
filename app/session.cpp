#include "session.h"

#include "time_cursor.h"

#include <algorithm>

namespace abrviz {

Session::Session(QObject* parent)
    : QObject(parent)
    , cursor_(new TimeCursor(this))
{
}

Session::~Session() = default;

void Session::setRun(std::shared_ptr<const RunData> run, QStringList loadErrors)
{
    run_ = std::move(run);
    loadErrors_ = std::move(loadErrors);
    clock_ = run_ ? PlaybackClock(*run_) : PlaybackClock();
    report_ = run_ ? computeQoe(*run_) : QoeReport();
    cursor_->setRange(0.0, endTime());
    cursor_->setTime(run_ ? run_->playbackStart : 0.0, TimeCursor::Source::Program);
    emit runChanged();
}

void Session::clear()
{
    setRun(nullptr);
}

double Session::endTime() const
{
    if (!run_)
        return 0.0;
    double end = std::max(run_->playbackEnd, clock_.simEnd());
    if (!run_->segments.empty())
        end = std::max(end, run_->segments.back().doneTime);
    return end;
}

} // namespace abrviz
