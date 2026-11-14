#include "time_cursor.h"

#include <algorithm>

namespace abrviz {

TimeCursor::TimeCursor(QObject* parent)
    : QObject(parent)
{
}

void TimeCursor::setRange(double minimum, double maximum)
{
    minimum_ = minimum;
    maximum_ = std::max(minimum, maximum);
    setTime(time_, Source::Program);
}

void TimeCursor::setTime(double time, Source source)
{
    const double clamped = std::clamp(time, minimum_, maximum_);
    if (clamped == time_)
        return;
    time_ = clamped;
    emit timeChanged(time_, source);
}

} // namespace abrviz
