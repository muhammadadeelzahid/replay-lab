#pragma once

/**
 * @file
 * @brief One time for several runs played side by side.
 */

#include "abrviz/core/model/run_data.h"
#include "abrviz/core/playback/playback_clock.h"

#include <memory>
#include <vector>

namespace abrviz {

/**
 * @brief Maps one master time to each run's replay time, so that several runs of the same
 *        video can be played together.
 *
 * Two alignments:
 * - **Session time**: seconds since each run's stream start. Every pane shows what its user
 *   saw at the same moment of the session, so a stall shows as one pane freezing while the
 *   others play.
 * - **Media time**: seconds into the video. Every pane shows the same frame (at the rung its
 *   run played it), so only picture quality differs; stalls take no media time.
 *
 * Built once from the runs and then read-only, so it can be shared between threads.
 */
class ComparisonClock {
public:
    /** @brief How the runs are lined up. */
    enum class Alignment {
        SessionTime,  ///< the same moment since each stream started
        MediaTime,    ///< the same frame of the video
    };

    /**
     * @brief Where one run is at a master time.
     */
    struct RunPosition {
        double replay = 0.0;  ///< the run's replay time, in seconds (clamped to its replay)
        double sim = 0.0;     ///< the run's simulation time, in seconds
        PlaybackClock::Position position;  ///< what the run shows there
        bool starting = false;  ///< before the run's first frame (start-up)
        bool ended = false;     ///< after the run's last frame (or where it was cut off)
    };

    /**
     * @brief Creates a clock with no runs.
     */
    ComparisonClock() = default;

    /**
     * @brief Builds the clock for some runs.
     *
     * @param runs The runs, in pane order. Null entries are kept as empty runs.
     * @param alignment How to line them up.
     */
    ComparisonClock(std::vector<std::shared_ptr<const RunData>> runs, Alignment alignment);

    /**
     * @brief Returns the alignment.
     *
     * @return How the runs are lined up.
     */
    [[nodiscard]] Alignment alignment() const { return alignment_; }

    /**
     * @brief Returns the number of runs.
     *
     * @return The run count.
     */
    [[nodiscard]] int runCount() const { return static_cast<int>(entries_.size()); }

    /**
     * @brief Returns a run.
     *
     * @param index Zero-based run index.
     * @return The run, or null if `index` is out of range or the run is empty.
     */
    [[nodiscard]] const std::shared_ptr<const RunData>& run(int index) const;

    /**
     * @brief Returns a run's own playback clock.
     *
     * @param index Zero-based run index.
     * @return The clock; an empty clock if `index` is out of range.
     */
    [[nodiscard]] const PlaybackClock& clock(int index) const;

    /**
     * @brief Returns the length of the master timeline: the longest run.
     *
     * @return Seconds of session time or of media, depending on the alignment.
     */
    [[nodiscard]] double duration() const { return duration_; }

    /**
     * @brief Returns where a run is at a master time.
     *
     * @param index Zero-based run index.
     * @param master Master time in seconds; clamped to the master timeline (NaN counts as 0,
     *               infinities clamp to the ends).
     * @return The run's position; all zero if `index` is out of range or the run is empty.
     */
    [[nodiscard]] RunPosition positionOf(int index, double master) const;

    /**
     * @brief Returns the master time of a run's replay time, the inverse of `positionOf()`.
     *
     * In media time, a replay time inside a stall maps to the media time of the stall.
     *
     * @param index Zero-based run index.
     * @param replay The run's replay time in seconds.
     * @return The master time, in [0, duration()]; 0 if `index` is out of range.
     */
    [[nodiscard]] double masterOf(int index, double replay) const;

private:
    /** @brief One run and its clock. */
    struct Entry {
        std::shared_ptr<const RunData> run;  ///< null for an empty pane
        PlaybackClock clock;                 ///< the run's own clock
    };

    std::vector<Entry> entries_;  ///< in pane order
    Alignment alignment_ = Alignment::SessionTime;
    double duration_ = 0.0;  ///< length of the master timeline in seconds
};

} // namespace abrviz
