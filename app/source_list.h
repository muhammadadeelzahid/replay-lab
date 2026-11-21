#pragma once

/**
 * @file
 * @brief The window's source-list sidebar.
 */

#include "abrviz/core/load/bundle_library.h"
#include "abrviz/core/metrics/highlights.h"

#include <QTreeWidget>

namespace abrviz {

/**
 * @brief The window's sidebar, in the style of a macOS source list.
 *
 * A Library section holds All Runs, then there is one section per video listing its runs by
 * link rate. Selecting All Runs shows the library; selecting a run opens it. Each run shows a
 * short note on the right (stalls or a cut-off); runs with load errors are disabled.
 */
class SourceList final : public QTreeWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the sidebar with the Library section and its All Runs item.
     *
     * @param parent The parent widget.
     */
    explicit SourceList(QWidget* parent = nullptr);

    /**
     * @brief Replaces the runs, without emitting signals.
     *
     * A new section starts whenever the video title changes, so `runs` should be grouped by
     * video.
     *
     * @param runs The runs, in library order.
     */
    void setRuns(const QList<BundleSummaryPtr>& runs);

    /**
     * @brief Selects the run in `directory` without emitting a signal.
     *
     * @param directory The run's bundle directory; empty selects All Runs. A directory that is
     *                  not listed leaves the selection unchanged.
     */
    void selectRun(const QString& directory);

    /**
     * @brief Selects All Runs without emitting a signal.
     */
    void selectAllRuns();

    /**
     * @brief The number of run items.
     *
     * @return The run count, across all video sections.
     */
    [[nodiscard]] int runCount() const;

    /**
     * @brief The text of a run item and its section.
     *
     * @param directory The run's bundle directory.
     * @return "Section / Run", or an empty string if the run is not listed.
     */
    [[nodiscard]] QString runLabel(const QString& directory) const;

signals:
    /**
     * @brief Emitted when the user selects All Runs.
     */
    void allRunsSelected();

    /**
     * @brief Emitted when the user selects a run.
     *
     * @param directory The run's bundle directory.
     */
    void runSelected(const QString& directory);

    /**
     * @brief Compare Side by Side was chosen from a run's menu.
     *
     * @param directory The bundle's directory.
     */
    void compareRequested(const QString& directory);

    /**
     * @brief A highlight was selected: open its run next to the rival at the moment.
     *
     * @param directory The run's directory.
     * @param rival The directory of the run to show next to it.
     * @param time Simulation time of the run to start at.
     */
    void compareAtRequested(const QString& directory, const QString& rival, double time);

public:
    /**
     * @brief Lists the experiments where the LLM looks better in a section above the videos.
     *
     * @param highlights Best first; empty removes the section.
     * @param runs The runs, for each highlight's video title and rate.
     */
    void setHighlights(const std::vector<Highlight>& highlights, const QList<BundleSummaryPtr>& runs);

    /// @return The number of highlights listed.
    [[nodiscard]] int highlightCount() const;

protected:
    /**
     * @brief Restyles the list when the palette changes (light/dark switch).
     *
     * @param event The change event.
     */
    void changeEvent(QEvent* event) override;

    /**
     * @brief Offers Play and Compare Side by Side for the run under the pointer.
     *
     * @param event The context-menu event.
     */
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    /** @brief Item data roles. */
    enum Role { KindRole = Qt::UserRole + 1, DirectoryRole, RivalRole, TimeRole };
    /** @brief What an item is, stored under KindRole. */
    enum Kind { Section, AllRuns, Run, HighlightRun };

    QTreeWidgetItem* highlights_ = nullptr;  ///< The highlights section; null if none.

    /**
     * @brief Appends a bold, non-selectable section heading.
     *
     * @param title The heading text.
     * @return The new top-level item.
     */
    QTreeWidgetItem* addSection(const QString& title);

    /**
     * @brief Emits allRunsSelected() or runSelected() for a new current item.
     *
     * Does nothing while selectRun() is selecting, or for a section heading.
     *
     * @param current The new current item; may be null.
     */
    void itemChanged(QTreeWidgetItem* current);

    /**
     * @brief Applies the colours for the appearance: secondary-text section headings, accent
     *        icons on the items, and the sidebar's background and selection style sheet.
     */
    void applyStyle();
    bool restyling_ = false;  ///< Guards changeEvent(): setting a style sheet changes the palette.

    /**
     * @brief Finds the item of a run.
     *
     * @param directory The run's bundle directory.
     * @return The run's item, or null if it is not listed.
     */
    [[nodiscard]] QTreeWidgetItem* findRun(const QString& directory) const;

    QTreeWidgetItem* allRuns_ = nullptr;  ///< "All Runs" item under Library; owned by the tree.
    bool selecting_ = false;  ///< True while selectRun() changes the current item.
};

} // namespace abrviz
