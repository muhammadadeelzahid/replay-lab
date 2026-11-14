#pragma once

#include "abrviz/media/replay_source_factory.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QLabel;

namespace abrviz {

/// The Settings window (⌘,): the replay engine, the video overlay, and where rendered videos
/// are kept. Changes apply at once, as in macOS settings windows.
class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const QString& renderCache, QWidget* parent = nullptr);

    void setEngine(Engine engine);
    void setOverlayVisible(bool visible);

signals:
    void engineChosen(abrviz::Engine engine);
    void overlayToggled(bool visible);

private:
    QButtonGroup* engines_;
    QCheckBox* overlay_;
    QLabel* cache_;
};

} // namespace abrviz
