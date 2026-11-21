#pragma once

/**
 * @file
 * @brief The application window.
 */

#include "abrviz/media/replay_source_factory.h"

#include "abrviz/core/load/bundle_library.h"

#include <QMainWindow>

#include <memory>

class QAction;

namespace abrviz {

class CompareView;

class ChartsPanel;
class LibraryView;
class LogConsole;
class MomentPanel;
class RenderQueue;
class ReplayPlayer;
class ReportView;
class Session;
class SegmentTimeline;
class SourceList;
class SummaryPanel;
class TransportBar;

/**
 * @brief The application window, laid out as macOS apps are.
 *
 * The window has a source-list sidebar (All Runs, then the runs of each video), a unified
 * toolbar with a segmented control for the views of the open run (Replay, Analysis, Logs,
 * Report), an inspector on the right showing the moment at the cursor, and a transport bar
 * that plays the replay and holds the segment timeline, which moves the shared time cursor.
 * Folders and bundles load in the background; progress and problems show in a banner over the
 * content. The Library page shows the welcome screen or all runs.
 *
 * Shutdown order: the destructor calls shutdown() first, which cancels and waits for the
 * pending scan, load and log indexing, and then destroys the replay engine. Only then is the
 * private state destroyed (with the render queue the engine used), and after it the child
 * widgets, so no task finishes into a half-destroyed window.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /** @brief The pages of the window, in the order of their ⌘1 to ⌘5 shortcuts. */
    enum class Page { Library, Replay, Analysis, Logs, Report };
    static constexpr int kPageCount = 5;  ///< Number of values in Page.

    /**
     * @brief Builds the window, its pages, actions, menus and toolbar.
     *
     * Restores the saved engine, geometry, overlay, sidebar, inspector and recent folders, and
     * shows the Library page. The environment variables ABRVIZ_RENDER_CACHE and ABRVIZ_FFMPEG
     * override where rendered videos are kept and which ffmpeg renders them (the tests use
     * both).
     *
     * @param parent The parent widget; normally null.
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Calls shutdown(), then destroys the private state and the child widgets.
     *
     * @see shutdown() for the shutdown order.
     */
    ~MainWindow() override;

    /** @brief Not copyable or movable. */
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /**
     * @brief Opens a folder of bundles or a single bundle.
     *
     * The path is always scanned and listed in the library; if it is a bundle (it holds a
     * `run.json`), it is also opened.
     *
     * @param path A folder of bundles or a bundle directory.
     */
    void openPath(const QString& path);

    /**
     * @brief Lists the bundles in a folder, reading their summaries in parallel.
     *
     * Runs in the background; the library shows the folder as being read until the scan
     * finishes. When it does, the library and sidebar are filled, the folder is added to the
     * recent folders, and folderScanned() is emitted. Starting another scan cancels this one.
     * Does nothing after shutdown().
     *
     * @param folder The folder to scan.
     */
    void openFolder(const QString& folder);

    /**
     * @brief Opens a run side by side with another algorithm of its experiment.
     *
     * @param directory The bundle's directory.
     */
    void compareBundle(const QString& directory);

    /**
     * @brief Opens a run side by side with another one, lined up by video frame, at a moment.
     *
     * @param directory The run to open.
     * @param rival The run of the same experiment to show next to it.
     * @param time Simulation time of the open run to start at.
     */
    void compareAt(const QString& directory, const QString& rival, double time);

    /**
     * @brief Loads the bundle in a directory and makes it the open run when the load finishes.
     *
     * Opening another bundle first cancels this one. When the load succeeds, log indexing
     * starts and the window leaves the Library page for Replay; on failure a notice says so.
     * bundleOpened() is emitted either way, unless the load was cancelled. Does nothing after
     * shutdown().
     *
     * @param directory The bundle directory.
     */
    void openBundle(const QString& directory);

    /**
     * @brief Switches the replay engine.
     *
     * @param engine The engine to use.
     * @param remember Whether to save the choice for the next start (the Engine menu and the
     *                 Settings window do this; `--engine` does not).
     * @return False if `engine` is not built; true otherwise.
     */
    bool setEngine(Engine engine, bool remember = false);

    /**
     * @brief The engine saved in the settings, or the default one.
     *
     * @return The saved engine if it is built; ReplaySourceFactory::defaultEngine() otherwise.
     */
    [[nodiscard]] static Engine savedEngine();

    /**
     * @brief Shows a page and syncs the page actions, the segmented control and the sidebar.
     *
     * The Library page selects All Runs in the sidebar; the other pages select the open run.
     *
     * @param page The page to show.
     */
    void showPage(Page page);

    /**
     * @brief The page shown.
     *
     * @return The current page.
     */
    [[nodiscard]] Page currentPage() const;

    /**
     * @brief The translated name of a page, as the segmented control and View menu show it.
     *
     * @param page The page.
     * @return Its name, e.g. "Replay".
     */
    [[nodiscard]] static QString pageName(Page page);

    /**
     * @brief Cancels and waits for every pending task, then releases the replay engine.
     *
     * The watchers are disconnected first so no result arrives once shutdown has started.
     * After this, openFolder() and openBundle() do nothing.
     *
     * @note The engine is released here because it may run a job on the render queue, which
     *       the private state destroys next. Safe to call more than once.
     */
    void shutdown();

    /**
     * @brief The session holding the open run and the shared time cursor.
     *
     * @return The session, owned by the window.
     */
    [[nodiscard]] Session* session() const;

    /**
     * @brief The Library page.
     *
     * @return The view, owned by the window.
     */
    [[nodiscard]] LibraryView* library() const;

    /**
     * @brief The segment timeline in the transport bar.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] SegmentTimeline* timeline() const;

    /**
     * @brief The charts of the Analysis page.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] ChartsPanel* charts() const;

    /**
     * @brief The log console of the Logs page.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] LogConsole* logs() const;

    /**
     * @brief The replay player of the Replay page.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] ReplayPlayer* player() const;

    /**
     * @brief The queue that renders videos for the pre-rendered engine.
     *
     * @return The queue, owned by the window's private state.
     */
    [[nodiscard]] RenderQueue* renderQueue() const;

    /**
     * @brief The summary beside the report on the Report page.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] SummaryPanel* summary() const;

    /**
     * @brief The report of the Report page.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] ReportView* report() const;

    /**
     * @brief Returns the Replay page's video area: the open run alone or several side by side.
     *
     * @return The view; owned by the window.
     */
    [[nodiscard]] CompareView* compareView() const;

    /**
     * @brief The transport bar under the pages.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] TransportBar* transport() const;

    /**
     * @brief The inspector on the right.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] MomentPanel* moment() const;

    /**
     * @brief The source-list sidebar.
     *
     * @return Owned by the window.
     */
    [[nodiscard]] SourceList* sidebar() const;

    /**
     * @brief The banner text shown over the content (loading, errors, engine fallback).
     *
     * @return The text, or an empty string if the banner is hidden.
     */
    [[nodiscard]] QString noticeText() const;

    /**
     * @brief Shows a banner over the content.
     *
     * @param text The message; empty hides the banner.
     * @param milliseconds How long to show it; 0 or less keeps it until it is replaced.
     */
    void showNotice(const QString& text, int milliseconds = 8000);

    /**
     * @brief Whether a background task is running.
     *
     * @return True while a folder scan, a bundle load or log indexing is pending.
     */
    [[nodiscard]] bool isBusy() const;

    /**
     * @brief Moves the cursor by an amount of replay time.
     *
     * The result is clamped to the replay. Does nothing without a run.
     *
     * @param seconds The step in replay seconds; negative steps back (the step buttons use 5 s).
     */
    void stepReplay(double seconds);

    /**
     * @brief Moves the cursor to the start of the next or previous segment.
     *
     * Forward goes to the first segment that starts more than 0.01 s after the cursor (none:
     * no move). Back goes to the start of the segment before, unless the cursor is more than
     * 0.3 s into the current one, in which case it goes to the current segment's start; if no
     * segment starts that far back, it goes to the start of the simulation. Does nothing
     * without a run.
     *
     * @param direction 1 for the next segment, -1 for the previous one.
     */
    void stepSegment(int direction);

signals:
    /**
     * @brief The library list was replaced (after openFolder()).
     *
     * @param folder The absolute path of the scanned folder.
     */
    void folderScanned(const QString& folder);

    /// The scanned folder's highlights were found (possibly none) and shown on the home page.
    void highlightsFound();

    /**
     * @brief A load finished; the session holds the new run, or no run if the load failed.
     *
     * @param directory The absolute path of the bundle.
     * @param ok Whether the run was loaded.
     */
    void bundleOpened(const QString& directory, bool ok);

    /**
     * @brief The log index of the open run was built and handed to the session.
     */
    void logIndexReady();

protected:
    /**
     * @brief Saves the window geometry, then closes.
     *
     * @param event The close event.
     */
    void closeEvent(QCloseEvent* event) override;

    /**
     * @brief Restyles the window on a light/dark switch, redrawing the icons in the new text
     *        colour.
     *
     * @param event The change event; palette and application palette changes are handled.
     */
    void changeEvent(QEvent* event) override;

private:
    /**
     * @brief Creates the pages, the sidebar, the inspector, the transport bar and the notice
     *        banner, and lays them out in the central widget.
     *
     * @note Needs the actions, so it runs after createActions().
     */
    void createPages();

    /**
     * @brief Creates the window's actions and their shortcuts, the page actions and the
     *        engine actions.
     *
     * The actions are added to the window so their shortcuts work whichever view is shown.
     */
    void createActions();

    /**
     * @brief Creates the File, View, Playback, Window and Help menus.
     */
    void createMenus();

    /**
     * @brief Creates the toolbar: the sidebar toggle, Open Folder, the segmented control for
     *        the run's views, and the inspector toggle.
     */
    void createToolBar();

    /**
     * @brief Shows the Settings window, creating it on first use and syncing its values.
     */
    void showSettings();

    /**
     * @brief Sets the window title from the open run (video, feeder rate, network), or to
     *        the application name without a run.
     */
    void updateTitle();

    /**
     * @brief Shows the inspector (with a run, when enabled, and not on the Library page) and
     *        the transport bar (with a run).
     */
    void updatePanes();

    /**
     * @brief Redraws the icons in the palette's text colour and sets the window's style
     *        sheet for the light or dark appearance.
     */
    void applyStyle();

    /**
     * @brief Asks for a folder of runs and opens it.
     */
    void chooseFolder();

    /**
     * @brief Asks for a run bundle and opens it with openPath().
     */
    void chooseBundle();

    /**
     * @brief Handles the end of a folder scan.
     *
     * Fills the library and sidebar, remembers the folder and emits folderScanned(); a
     * cancelled scan only updates the banner.
     */
    void scanFinished();

    /// Starts finding the experiments where the LLM looks better, for the home page.
    void startHighlights(const QList<BundleSummaryPtr>& bundles);

    /**
     * @brief Handles the end of a bundle load.
     *
     * Hands the run (or the failure) to the session, starts log indexing on success, and
     * emits bundleOpened(); a cancelled load only updates the banner.
     */
    void loadFinished();

    /**
     * @brief Starts building the log index of the open run in the background.
     *
     * @note Requires an open run.
     */
    void startLogIndex();

    /**
     * @brief Hands a finished log index to the session and emits logIndexReady().
     *
     * The session ignores an index for a run that is no longer open.
     */
    void logIndexFinished();

    /**
     * @brief Shows "Opening..." or "Reading..." in the banner while a load or scan runs, and
     *        hides the banner afterwards unless a timed notice is still showing.
     */
    void updateStatus();

    /**
     * @brief Enables the playback, step, render and page actions for the run and engine state,
     *        and switches the play action between Play and Pause.
     */
    void updateActions();

    /**
     * @brief Checks the engine in use in the Engine menu and the Settings window.
     */
    void updateEngineActions();

    /**
     * @brief Fills the sidebar with the library's runs and selects All Runs or the open run.
     */
    void updateSidebar();

    /**
     * @brief Moves a folder to the top of the recent folders (at most six) and refreshes the
     *        Open Recent menu and the welcome screen.
     *
     * @param folder The folder to remember.
     */
    void rememberFolder(const QString& folder);

    /**
     * @brief Restores the geometry, overlay, sidebar and inspector settings and rebuilds the
     *        recent-folder lists (Open Recent menu and welcome screen).
     */
    void restoreSettings();

    /** @brief The window's private state, defined in main_window.cpp. */
    struct Impl;
    std::unique_ptr<Impl> d_;  ///< Private state; created by the constructor, never null.
};

} // namespace abrviz
