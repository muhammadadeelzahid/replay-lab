#pragma once

/**
 * @file
 * @brief The illustration beside the welcome screen: the satellite path at dusk.
 */

#include <QWidget>

class QTimer;

namespace abrviz {

/**
 * @brief Paints the network every run was streamed over as a winter scene at dusk.
 *
 * A ground-station dish (the video server) and an IAB relay mast stand on the snow; a LEO
 * satellite crosses the sky along its orbit, with beams to the dish and the relay that follow
 * it, and the relay serves a viewer. The scene is the same in light and dark mode. The
 * satellite and the dashes on the links move only while the widget is visible.
 */
class WelcomeScene final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the scene.
     *
     * @param parent The parent widget.
     */
    explicit WelcomeScene(QWidget* parent = nullptr);

    /**
     * @brief The preferred size: a tall panel.
     *
     * @return 520 x 760 pixels.
     */
    QSize sizeHint() const override;

    /**
     * @brief The smallest useful size.
     *
     * @return 340 x 420 pixels.
     */
    QSize minimumSizeHint() const override;

    /**
     * @brief Whether the satellite and the dashes are moving, for tests.
     *
     * @return True while the animation timer is active.
     */
    [[nodiscard]] bool isAnimating() const;

    /**
     * @brief How far the satellite is along its pass across the sky, for tests.
     *
     * @return A value in [0, 1): 0 as it rises on the left, close to 1 as it sets on the right.
     */
    [[nodiscard]] double passProgress() const { return progress_; }

protected:
    /**
     * @brief Paints the sky, the snow, the nodes, the links and their names.
     *
     * @param event The paint event (unused; the whole widget is painted).
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief Starts the animation.
     *
     * @param event The show event.
     */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Stops the animation while the scene is hidden.
     *
     * @param event The hide event.
     */
    void hideEvent(QHideEvent* event) override;

private:
    QTimer* animation_;      ///< Moves the satellite and the dashes about 30 times a second.
    double progress_ = 0.35; ///< The satellite's place along its pass, in [0, 1).
    double phase_ = 0.0;     ///< Dash offset of the links, in pixels.
};

} // namespace abrviz
