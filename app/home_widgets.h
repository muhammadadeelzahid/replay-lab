#pragma once

/**
 * @file
 * @brief The library's home page: the featured run, the path it was streamed over, and the
 *        cards of the runs.
 */

#include "abrviz/core/load/bundle_library.h"

#include <QAbstractButton>
#include <QImage>
#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace abrviz {

/**
 * @brief The network a run was streamed over, as a glass panel laid over the featured run.
 *
 * Four nodes from left to right (video server, LEO satellite gNB, IAB relay, user on a laptop)
 * with the links between them named, and dashes that flow towards the user while the panel is
 * visible.
 */
class PathPanel final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the panel.
     *
     * @param parent The parent widget.
     */
    explicit PathPanel(QWidget* parent = nullptr);

    /**
     * @brief Sets the numbers shown on the links.
     *
     * @param feeder The feeder link, e.g. "36 Mbps feeder"; empty for "Feeder link".
     * @param satellite The satellite's note, e.g. "550 km up".
     * @param backhaul The backhaul link, e.g. "28 GHz backhaul".
     */
    void setDetails(const QString& feeder, const QString& satellite, const QString& backhaul);

    /// @return 520 x 124 pixels.
    QSize sizeHint() const override;

    /**
     * @brief The texts painted on the panel, for tests.
     *
     * @return The heading, the four node names and the three link names.
     */
    [[nodiscard]] QStringList texts() const;

protected:
    /// Paints the glass, the links, the nodes and their names.
    void paintEvent(QPaintEvent* event) override;
    /// Starts the dashes.
    void showEvent(QShowEvent* event) override;
    /// Stops the dashes.
    void hideEvent(QHideEvent* event) override;

private:
    QTimer* animation_;   ///< Moves the dashes.
    double phase_ = 0.0;  ///< Dash offset, in pixels.
    QString feeder_;      ///< Feeder link name.
    QString satellite_;   ///< Note under the satellite.
    QString backhaul_;    ///< Backhaul link name.
};

/**
 * @brief A run's card: a still from its video with its feeder rate and how playback went.
 *
 * Clicking the card emits clicked(); double-clicking it emits opened(). The featured run's
 * card has a blue outline.
 */
class RunCard final : public QAbstractButton {
    Q_OBJECT

public:
    /**
     * @brief Creates the card.
     *
     * @param bundle The run.
     * @param parent The parent widget.
     */
    explicit RunCard(BundleSummaryPtr bundle, QWidget* parent = nullptr);

    /// @return The run.
    [[nodiscard]] const BundleSummaryPtr& bundle() const { return bundle_; }

    /// @param image The still from the run's video; null for a plain card.
    void setImage(const QImage& image);

    /// @param featured Whether this run is the one shown above the rows.
    void setFeatured(bool featured);

    /// @return Whether this run is the one shown above the rows.
    [[nodiscard]] bool isFeatured() const { return featured_; }

    /// @return 256 x 144 pixels.
    QSize sizeHint() const override;

signals:
    /// The card was double-clicked: open the run.
    void opened();

protected:
    /// Paints the still, the shade, the names and the badge.
    void paintEvent(QPaintEvent* event) override;
    /// Emits opened().
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    /// Repaints with the hover outline.
    void enterEvent(QEnterEvent* event) override;
    /// Repaints without the hover outline.
    void leaveEvent(QEvent* event) override;

private:
    BundleSummaryPtr bundle_;  ///< The run.
    QImage image_;             ///< The still, or null.
    bool featured_ = false;    ///< Whether it is the featured run.
    bool hovered_ = false;     ///< Whether the pointer is over the card.
};

/**
 * @brief The featured run, large: a still from its video, its title and numbers, Play and
 *        More Info, and the path it was streamed over.
 */
class FeaturedBanner final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates the banner, empty until setBundle().
     *
     * @param parent The parent widget.
     */
    explicit FeaturedBanner(QWidget* parent = nullptr);

    /**
     * @brief Shows a run.
     *
     * @param bundle The run; null to clear the banner.
     * @param satellite The satellite's note on the path panel, e.g. "550 km up".
     * @param backhaul The backhaul's name on the path panel, e.g. "28 GHz backhaul".
     */
    void setBundle(BundleSummaryPtr bundle, const QString& satellite, const QString& backhaul);

    /// @return The run shown, or null.
    [[nodiscard]] const BundleSummaryPtr& bundle() const { return bundle_; }

    /// @param image The still from the run's video; null for the plain background.
    void setImage(const QImage& image);

    /// @return The title label, for tests.
    [[nodiscard]] QLabel* titleLabel() const { return title_; }

    /// @return The path panel, for tests.
    [[nodiscard]] PathPanel* pathPanel() const { return path_; }

signals:
    /// Play was clicked: open the run and replay it.
    void playRequested();
    /// More Info was clicked: open the run and show its report.
    void infoRequested();

protected:
    /// Paints the still with the shades that keep the text readable.
    void paintEvent(QPaintEvent* event) override;
    /// Keeps the path panel in the bottom right corner.
    void resizeEvent(QResizeEvent* event) override;

private:
    BundleSummaryPtr bundle_;  ///< The run shown.
    QImage image_;             ///< Its still.
    QLabel* title_;            ///< The video's title.
    QLabel* meta_;             ///< Feeder rate, segments and policy.
    QLabel* highlight_;        ///< How playback went.
    QLabel* description_;      ///< What happened in the run.
    QPushButton* play_;        ///< Opens the run.
    QPushButton* info_;        ///< Opens the run's report.
    PathPanel* path_;          ///< The network, bottom right.
};

} // namespace abrviz
