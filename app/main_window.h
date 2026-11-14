#pragma once

#include <QMainWindow>

#include <memory>

namespace abrviz {

class ChartsPanel;
class LibraryView;
class Session;
class SegmentTimeline;
class SummaryPanel;

/// The application window: docks for the library, the run summary, the timeline and the charts,
/// and the loading of folders and bundles in the background.
///
/// Shutdown order: the destructor calls `shutdown()` first, which cancels and waits for the
/// pending scan and load. Only then are the private state and the child widgets destroyed, so
/// no task finishes into a half-destroyed window.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
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

    /// Cancels and waits for every pending task. Safe to call more than once.
    void shutdown();

    [[nodiscard]] Session* session() const;
    [[nodiscard]] LibraryView* library() const;
    [[nodiscard]] SegmentTimeline* timeline() const;
    [[nodiscard]] ChartsPanel* charts() const;
    [[nodiscard]] SummaryPanel* summary() const;
    [[nodiscard]] bool isBusy() const;

signals:
    /// The library list was replaced (after `openFolder`).
    void folderScanned(const QString& folder);
    /// A load finished; the session holds the new run, or no run if the load failed.
    void bundleOpened(const QString& directory, bool ok);

private:
    void createMenus();
    void createDocks();
    void chooseFolder();
    void chooseBundle();
    void scanFinished();
    void loadFinished();
    void updateStatus();

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace abrviz
