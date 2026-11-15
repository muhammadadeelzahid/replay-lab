#pragma once

/**
 * @file
 * @brief The shared simulation-time cursor that keeps the panels in step.
 */

#include <QObject>

namespace abrviz {

/**
 * @brief The simulation time every panel shows.
 *
 * A panel that moves the cursor passes itself as the source and ignores the change
 * notification it caused, which prevents feedback loops.
 */
class TimeCursor final : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Who moved the cursor.
     *
     * Panels compare the source of `timeChanged` with their own value to ignore the changes
     * they caused themselves.
     */
    enum class Source { Program, Timeline, Charts, Logs, Player };
    Q_ENUM(Source)

    /**
     * @brief Creates a cursor at time 0 with an empty range [0, 0].
     *
     * @param parent Owning QObject, or null.
     */
    explicit TimeCursor(QObject* parent = nullptr);

    /**
     * @brief Current cursor time.
     *
     * @return Simulation time in seconds, always within [minimum(), maximum()].
     */
    [[nodiscard]] double time() const { return time_; }
    /**
     * @brief Lower end of the allowed range.
     *
     * @return Simulation time in seconds.
     */
    [[nodiscard]] double minimum() const { return minimum_; }
    /**
     * @brief Upper end of the allowed range.
     *
     * @return Simulation time in seconds.
     */
    [[nodiscard]] double maximum() const { return maximum_; }

    /**
     * @brief Sets the allowed range and moves the cursor into it.
     *
     * If the cursor has to move, `timeChanged` is emitted with Source::Program.
     *
     * @param minimum Lower end in seconds.
     * @param maximum Upper end in seconds; raised to `minimum` if it is smaller.
     */
    void setRange(double minimum, double maximum);

public slots:
    /**
     * @brief Moves the cursor, clamped to the allowed range.
     *
     * Emits `timeChanged` only if the clamped time differs from the current one.
     * NaN is ignored.
     *
     * @param time New simulation time in seconds.
     * @param source Who moves the cursor; panels pass their own value so they can ignore the
     *        notification they cause.
     */
    void setTime(double time, abrviz::TimeCursor::Source source = Source::Program);

signals:
    /**
     * @brief Emitted when the cursor time changes.
     *
     * @param time New simulation time in seconds.
     * @param source Who moved the cursor; a receiver ignores changes it caused itself.
     */
    void timeChanged(double time, abrviz::TimeCursor::Source source);

private:
    double time_ = 0.0;     ///< Cursor time in simulation seconds, within the range.
    double minimum_ = 0.0;  ///< Start of the range, in seconds.
    double maximum_ = 0.0;  ///< End of the range, in seconds; never below minimum_.
};

} // namespace abrviz
