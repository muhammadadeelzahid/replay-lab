#pragma once

#include "abrviz/core/metrics/qoe_metrics.h"
#include "abrviz/core/model/run_data.h"
#include "abrviz/core/playback/playback_clock.h"

#include <QObject>
#include <QStringList>

#include <memory>

namespace abrviz {

class TimeCursor;

/// The open run and everything derived from it. Panels read from the session when
/// `runChanged` is emitted and keep their own `shared_ptr` to the run.
class Session final : public QObject {
    Q_OBJECT

public:
    explicit Session(QObject* parent = nullptr);
    ~Session() override;

    /// Replaces the open run. `loadErrors` are problems found while loading.
    void setRun(std::shared_ptr<const RunData> run, QStringList loadErrors = {});
    void clear();

    [[nodiscard]] const std::shared_ptr<const RunData>& run() const { return run_; }
    [[nodiscard]] const PlaybackClock& clock() const { return clock_; }
    [[nodiscard]] const QoeReport& report() const { return report_; }
    [[nodiscard]] const QStringList& loadErrors() const { return loadErrors_; }
    [[nodiscard]] TimeCursor* cursor() const { return cursor_; }

    /// End of the time axis shown by the panels.
    [[nodiscard]] double endTime() const;

signals:
    void runChanged();

private:
    std::shared_ptr<const RunData> run_;
    PlaybackClock clock_;
    QoeReport report_;
    QStringList loadErrors_;
    TimeCursor* cursor_;  // child object
};

} // namespace abrviz
