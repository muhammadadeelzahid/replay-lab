#pragma once

#include "abrviz/media/replay_source_factory.h"

#include <QMainWindow>

#include <memory>

class QAction;

namespace abrviz {

class ChartsPanel;
class LibraryView;
class LogConsole;
class MomentPanel;
class RenderQueue;
class ReplayPlayer;
class ReportView;
class Session;
class SegmentTimeline;
class SummaryPanel;
class TransportBar;

/// The application window. A sidebar switches between five pages: Library (the runs of a
/// folder, or a welcome screen), Replay (the video and what was on screen), Analysis (the
/// charts), Logs and Report. The toolbar opens folders and bundles, picks a run and the replay
/// engine; under every page the transport bar plays the replay and holds the segment timeline,
/// which moves the shared time cursor. Folders and bundles load in the background.
///
/// Shutdown order: the destructor calls `shutdown()` first, which cancels and waits for the
/// pending scan, load and log indexing, and then destroys the replay engine. Only then is the
/// private state destroyed (with the render queue the engine used), and after it the child
/// widgets, so no task finishes into a half-destroyed window.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    enum class Page { Library, Replay, Analysis, Logs, Report };
    static constexpr int kPageCount = 5;

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /// Opens `path`: a folder of bundles is listed in the library; a single bundle is listed and
    /// opened.
    void openPath(const QString& path);
    /// Lists the bundles in `folder`, reading their summaries in parallel.
    void openFolder(const QString& folder);
    /// Loads the bundle in `directory` and makes it the open run when the load finishes.
    /// Opening another bundle first cancels this one.
    void openBundle(const QString& directory);

    /// Switches the replay engine. With `remember`, the choice is saved for the next start
    /// (the Engine menu does this; `--engine` does not). Returns false if `engine` is not built.
    bool setEngine(Engine engine, bool remember = false);
    /// The engine saved in the settings, or the default one.
    [[nodiscard]] static Engine savedEngine();

    void showPage(Page page);
    [[nodiscard]] Page currentPage() const;
    [[nodiscard]] static QString pageName(Page page);

    /// Cancels and waits for every pending task. Safe to call more than once.
    void shutdown();

    [[nodiscard]] Session* session() const;
    [[nodiscard]] LibraryView* library() const;
    [[nodiscard]] SegmentTimeline* timeline() const;
    [[nodiscard]] ChartsPanel* charts() const;
    [[nodiscard]] LogConsole* logs() const;
    [[nodiscard]] ReplayPlayer* player() const;
    [[nodiscard]] RenderQueue* renderQueue() const;
    [[nodiscard]] SummaryPanel* summary() const;
    [[nodiscard]] ReportView* report() const;
    [[nodiscard]] TransportBar* transport() const;
    [[nodiscard]] MomentPanel* moment() const;
    [[nodiscard]] bool isBusy() const;

    /// Moves the cursor by `seconds` of replay time (the step buttons use 5 s).
    void stepReplay(double seconds);
    /// Moves the cursor to the start of the next (`direction` 1) or previous (-1) segment.
    void stepSegment(int direction);

signals:
    /// The library list was replaced (after `openFolder`).
    void folderScanned(const QString& folder);
    /// A load finished; the session holds the new run, or no run if the load failed.
    void bundleOpened(const QString& directory, bool ok);
    /// The log index of the open run was built and handed to the session.
    void logIndexReady();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void createPages();
    void createActions();
    void createMenus();
    void createToolBar();
    void applyStyle();
    void chooseFolder();
    void chooseBundle();
    void scanFinished();
    void loadFinished();
    void startLogIndex();
    void logIndexFinished();
    void updateStatus();
    void updateActions();
    void updateEngineActions();
    void updateRunPicker();
    void rememberFolder(const QString& folder);
    void restoreSettings();

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace abrviz
