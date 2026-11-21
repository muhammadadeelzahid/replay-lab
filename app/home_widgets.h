#pragma once

/**
 * @file
 * @brief The library's home page: the featured run, the path it was streamed over, and the
 *        cards of the runs.
 */

#include "abrviz/core/load/bundle_library.h"

#include <QAbstractButton>
#include <QPushButton>
#include <QImage>
#include <QWidget>

#include "icons.h"

class QLabel;
class QPushButton;
class QTimer;

namespace abrviz {

/**
 * @brief A large rounded button, as on the home page: blue for the main action, glass for the
 *        others.
 *
 * @param text The label.
 * @param glyph The icon, drawn in white.
 * @param primary Whether it is the main action.
 * @param parent The parent widget.
 * @return The new button, owned by `parent`.
 */
QPushButton* pillButton(const QString& text, Glyph glyph, bool primary, QWidget* parent);

/**
 * @brief A large card on the welcome page for one way of opening runs: an icon in a glass tile,
 *        a title, a short description and a chevron. The main card has a glowing blue outline.
 */
class SourceCard final : public QPushButton {
    Q_OBJECT

public:
    /**
     * @brief Creates the card.
     *
     * @param glyph The icon.
     * @param title The title, e.g. "Open a Folder".
     * @param detail One or two lines under the title.
     * @param primary Whether this is the main card.
     * @param parent The parent widget.
     */
    SourceCard(Glyph glyph, const QString& title, const QString& detail, bool primary, QWidget* parent);

    /// @return 300 x 210 pixels.
    QSize sizeHint() const override;

    /// @return The title, for tests.
    [[nodiscard]] const QString& title() const { return title_; }

protected:
    /// Paints the card.
    void paintEvent(QPaintEvent* event) override;
    /// Repaints with the hover look.
    void enterEvent(QEnterEvent* event) override;
    /// Repaints without the hover look.
    void leaveEvent(QEvent* event) override;

private:
    Glyph glyph_;           ///< The icon.
    QString title_;         ///< The title.
    QString detail_;        ///< The description.
    bool primary_;          ///< Whether this is the main card.
    bool hovered_ = false;  ///< Whether the pointer is over it.
};

/**
 * @brief The dark background of the welcome page: the window's ink with two faint glows of
 *        blue and indigo light, as in a macOS wallpaper.
 */
class GlowBackground final : public QWidget {
    Q_OBJECT

public:
    /// @param parent The parent widget.
    explicit GlowBackground(QWidget* parent = nullptr);

protected:
    /// Paints the ink and the glows.
    void paintEvent(QPaintEvent* event) override;
};

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

    /// @return 520 x 124 pixels (104 without a heading).
    QSize sizeHint() const override;

    /// @param heading The small heading in the top left corner; empty for none.
    void setHeading(const QString& heading);

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
    QString heading_;     ///< Heading in the top left corner; empty for none.
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
    /// The card was double-clicked, or Play chosen from its menu: open the run.
    void opened();
    /// Compare Side by Side was chosen from the card's menu.
    void compareRequested();

protected:
    /// Paints the still, the shade, the names and the badge.
    void paintEvent(QPaintEvent* event) override;
    /// Emits opened().
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    /// Repaints with the hover outline.
    void enterEvent(QEnterEvent* event) override;
    /// Offers Play and Compare.
    void contextMenuEvent(QContextMenuEvent* event) override;
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

    /**
     * @brief Shows the other algorithms of the featured run's experiment as chips, and the
     *        Compare button when there are any.
     *
     * @param runs The experiment's runs (the featured one included); fewer than two hides both.
     */
    void setExperiment(const QList<BundleSummaryPtr>& runs);

    /// @return The title label, for tests.
    [[nodiscard]] QLabel* titleLabel() const { return title_; }

    /// @return The path panel, for tests.
    [[nodiscard]] PathPanel* pathPanel() const { return path_; }

signals:
    /// Play was clicked: open the run and replay it.
    void playRequested();
    /// Compare was clicked: open the run side by side with another algorithm.
    void compareRequested();
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
    QLabel* meta_;             ///< Feeder rate and segments.
    QLabel* policy_ = nullptr; ///< The run's ABR algorithm, outlined.
    QLabel* highlight_;        ///< How playback went.
    QLabel* description_;      ///< What happened in the run.
    QPushButton* compare_;     ///< Opens the run side by side with another algorithm.
    QLabel* chips_;            ///< The experiment's algorithms and how each went.
    QPushButton* play_;        ///< Opens the run.
    QPushButton* info_;        ///< Opens the run's report.
    PathPanel* path_;          ///< The network, bottom right.
};

} // namespace abrviz
