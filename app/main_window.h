#pragma once

#include <QMainWindow>

namespace abrviz {

/// The application window. In step 0 it only has a menu bar, a placeholder and a status bar
/// that shows the version and the replay engines this build contains.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

private:
    void createMenus();
};

} // namespace abrviz
