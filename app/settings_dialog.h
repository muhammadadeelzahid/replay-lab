#pragma once

/**
 * @file
 * @brief The Settings window.
 */

#include "abrviz/media/replay_source_factory.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QLabel;

namespace abrviz {

/**
 * @brief The Settings window (⌘,): the replay engine, the video overlay, and where rendered
 *        videos are kept.
 *
 * Changes apply at once, as in macOS settings windows: the dialog emits a signal for each
 * change and has no OK or Cancel button.
 */
class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Builds the dialog.
     *
     * Engines that are not built are listed but disabled. The "Show in Finder" button creates
     * the cache folder if needed and opens it.
     *
     * @param renderCache The folder where rendered videos are kept.
     * @param parent The parent widget.
     */
    explicit SettingsDialog(const QString& renderCache, QWidget* parent = nullptr);

    /**
     * @brief Shows `engine` as the chosen engine, without emitting engineChosen().
     *
     * @param engine The engine in use.
     */
    void setEngine(Engine engine);

    /**
     * @brief Sets the overlay check box without emitting overlayToggled().
     *
     * @param visible Whether the overlay is shown over the video.
     */
    void setOverlayVisible(bool visible);

signals:
    /**
     * @brief Emitted when the user picks an engine.
     *
     * @param engine The engine picked.
     */
    void engineChosen(abrviz::Engine engine);

    /**
     * @brief Emitted when the user toggles the video overlay.
     *
     * @param visible Whether the overlay should be shown.
     */
    void overlayToggled(bool visible);

private:
    QButtonGroup* engines_;  ///< One radio button per engine; the button id is the Engine value.
    QCheckBox* overlay_;     ///< Checked when the video overlay is shown.
    QLabel* cache_;          ///< The render cache path.
};

} // namespace abrviz
