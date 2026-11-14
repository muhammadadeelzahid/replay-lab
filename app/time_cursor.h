#pragma once

#include <QObject>

namespace abrviz {

/// The simulation time every panel shows. A panel that moves the cursor passes itself as the
/// source and ignores the change notification it caused, which prevents feedback loops.
class TimeCursor final : public QObject {
    Q_OBJECT

public:
    enum class Source { Program, Timeline, Charts, Logs, Player };
    Q_ENUM(Source)

    explicit TimeCursor(QObject* parent = nullptr);

    [[nodiscard]] double time() const { return time_; }
    [[nodiscard]] double minimum() const { return minimum_; }
    [[nodiscard]] double maximum() const { return maximum_; }

    /// Sets the allowed range and moves the cursor into it.
    void setRange(double minimum, double maximum);

public slots:
    void setTime(double time, abrviz::TimeCursor::Source source = Source::Program);

signals:
    void timeChanged(double time, abrviz::TimeCursor::Source source);

private:
    double time_ = 0.0;
    double minimum_ = 0.0;
    double maximum_ = 0.0;
};

} // namespace abrviz
