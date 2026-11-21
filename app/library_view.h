#pragma once

/**
 * @file
 * @brief The Library page: the welcome screen and the table of runs in a folder.
 */

#include "abrviz/core/load/bundle_library.h"

#include <QAbstractTableModel>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QTableView;
class QVBoxLayout;

namespace abrviz {

class FeaturedBanner;
class RunCard;
class ThumbnailCache;

/**
 * @brief The rows of the library: one per bundle, sorted by video and link rate.
 *
 * The open bundle's row is shown in bold. Bundles with load errors show the errors as their
 * tool tip; the others show their directory.
 */
class LibraryModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    /** @brief The table's columns. */
    enum Column { Video, Algorithm, LinkRate, Segments, Stalls, CutOff, LogAgreement, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    /**
     * @brief Replaces the rows, sorting them into library order (resets the model).
     *
     * @param bundles The bundle summaries.
     */
    void setBundles(QList<BundleSummaryPtr> bundles);

    /**
     * @brief Marks the open bundle, whose row is drawn in bold.
     *
     * @param directory The bundle's directory; made absolute before it is compared.
     */
    void setOpenBundle(const QString& directory);

    /**
     * @brief The bundle of a row.
     *
     * @param row The source-model row.
     * @return The bundle summary, or null if `row` is out of range.
     */
    [[nodiscard]] BundleSummaryPtr bundleAt(int row) const;

    /**
     * @brief The row of a bundle.
     *
     * @param directory The bundle's directory; made absolute before it is compared.
     * @return The source-model row, or -1 if the bundle is not listed.
     */
    [[nodiscard]] int rowOf(const QString& directory) const;

    /**
     * @brief The number of bundles.
     *
     * @param parent Must be invalid (the model is a flat table).
     * @return The bundle count, or 0 for a valid `parent`.
     */
    int rowCount(const QModelIndex& parent = {}) const override;

    /**
     * @brief The number of columns.
     *
     * @param parent Must be invalid (the model is a flat table).
     * @return ColumnCount, or 0 for a valid `parent`.
     */
    int columnCount(const QModelIndex& parent = {}) const override;

    /**
     * @brief A cell's data.
     *
     * Supports the display, tool-tip, font (bold for the open bundle) and text-alignment
     * (right-aligned except the Video column) roles.
     *
     * @param index The cell.
     * @param role The data role.
     * @return The value, or an invalid QVariant for other roles or an invalid row.
     */
    QVariant data(const QModelIndex& index, int role) const override;

    /**
     * @brief A column title.
     *
     * @param section The column.
     * @param orientation Only Qt::Horizontal has titles.
     * @param role Only Qt::DisplayRole has titles.
     * @return The title, or an invalid QVariant otherwise.
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<BundleSummaryPtr> bundles_;  ///< The table's rows, in the order of sortLibrary().
    QString openBundle_;  ///< Absolute directory of the open bundle.
};

/**
 * @brief The Library page.
 *
 * Without a folder it shows a welcome screen (open a folder or a bundle, recent folders); with
 * one, a searchable table of its runs and the details of the selected run. Double-click, Enter
 * or the Open button opens a run.
 */
class LibraryView final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Builds the page, showing the welcome screen.
     *
     * @param parent The parent widget.
     */
    explicit LibraryView(QWidget* parent = nullptr);

    /**
     * @brief Shows the runs of a folder in the table and switches to it.
     *
     * Selects the first row if nothing is selected.
     *
     * @param bundles The bundle summaries of the folder.
     * @param folder The folder, shown with the run count under the title.
     */
    void setBundles(QList<BundleSummaryPtr> bundles, const QString& folder);

    /**
     * @brief Switches to the table and says that `folder` is being read.
     *
     * @param folder The folder being scanned.
     */
    void setScanning(const QString& folder);

    /**
     * @brief Marks the open bundle in bold and selects its row.
     *
     * @param directory The bundle's directory.
     */
    void setOpenBundle(const QString& directory);

    /**
     * @brief Replaces the Recent list of the welcome screen.
     *
     * @param folders The recent folders, most recent first; an empty list hides the section.
     */
    void setRecentFolders(const QStringList& folders);

    /**
     * @brief The model behind the table.
     *
     * @return The model, owned by the view.
     */
    [[nodiscard]] LibraryModel* model() const { return model_; }

    /**
     * @brief The search field above the table.
     *
     * @return The line edit, owned by the view.
     */
    [[nodiscard]] QLineEdit* searchEdit() const { return search_; }

    /**
     * @brief The number of rows shown after the search filter.
     *
     * @return The visible row count.
     */
    [[nodiscard]] int visibleRows() const;

    /**
     * @brief Whether the welcome screen is shown rather than the table.
     *
     * @return True while the welcome screen is shown.
     */
    [[nodiscard]] bool isShowingWelcome() const;

    /**
     * @brief The details card's text, for tests.
     *
     * @return The card's rich text.
     */
    [[nodiscard]] QString detailsText() const;

    /**
     * @brief Selects the row of a bundle.
     *
     * @param directory The bundle's directory; nothing happens if it is not listed or is
     *                  filtered out.
     */
    void selectBundle(const QString& directory);

signals:
    /**
     * @brief Emitted when the user opens a run (double-click, Enter or the Open button).
     *
     * @param directory The run's bundle directory.
     */
    void openRequested(const QString& directory);

    /**
     * @brief Emitted when the user clicks "Open a Folder of Runs..." on the welcome screen.
     */
    void openFolderRequested();

    /**
     * @brief Emitted when the user clicks "Open a Single Run..." on the welcome screen.
     */
    void openBundleRequested();

    /**
     * @brief Emitted when the user clicks a recent folder on the welcome screen.
     *
     * @param folder The folder, as given to setRecentFolders().
     */
    void recentFolderRequested(const QString& folder);

    /**
     * @brief More Info was clicked on the featured run: open it and show its report.
     *
     * @param directory The bundle's folder.
     */
    void infoRequested(const QString& directory);

    /**
     * @brief Compare was chosen for a run: open it side by side with another algorithm.
     *
     * @param directory The bundle's folder.
     */
    void compareRequested(const QString& directory);

protected:
    /**
     * @brief Opens the selected run when Return or Enter is pressed in the table.
     *
     * The table's own handling differs by platform (on macOS Return tries to edit the cell and
     * never emits `activated`), so the key is handled here, once, on every platform.
     *
     * @param watched The object the event is for.
     * @param event The event.
     * @return True if the event was handled.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

public:
    /**
     * @brief Makes stills of the runs' videos for the cards and the featured run.
     *
     * Without this the cards are plain.
     *
     * @param ffmpeg The `ffmpeg` program that takes the stills.
     * @param folder Where to keep them; empty for the user's cache folder.
     */
    void setThumbnailSource(const QString& ffmpeg, const QString& folder);

    /// @return The featured run's banner, for tests.
    [[nodiscard]] FeaturedBanner* featuredBanner() const { return banner_; }

    /// @return The run cards, in row order, for tests.
    [[nodiscard]] const QList<RunCard*>& runCards() const { return cards_; }

    /**
     * @brief Shows a run in the banner above the rows and outlines its card.
     *
     * @param bundle The run; null to clear the banner.
     */
    void feature(const BundleSummaryPtr& bundle);

private:
    /// Rebuilds the rows of cards from bundles_ and picks the featured run.
    void rebuildHome();

    /**
     * @brief Shows the details of the newly selected run.
     */
    void selectionChanged();

    /**
     * @brief Fills the details card and enables the Open button for a loadable run.
     *
     * @param bundle The run to describe; null shows a hint to select a run.
     */
    void showDetails(const BundleSummaryPtr& bundle);

    /**
     * @brief The bundle of the table's current row.
     *
     * @return The bundle summary, or null if no row is current.
     */
    [[nodiscard]] BundleSummaryPtr selectedBundle() const;

    LibraryModel* model_;           ///< All bundles of the library; child of the view.
    QSortFilterProxyModel* proxy_;  ///< Filters model_ by the search text, in any column.
    QStackedWidget* stack_;         ///< Holds welcome_ and browser_.
    QWidget* welcome_;              ///< Welcome screen; shown until a folder is opened.
    QWidget* recentBox_;            ///< The Recent list on the welcome screen.
    QWidget* browser_;              ///< The table page.
    QLabel* title_;                 ///< "All Runs" heading above the table.
    QLabel* path_;                  ///< Run count and folder, or the scanning message.
    QLineEdit* search_;             ///< Search field; its text filters the table through proxy_.
    QTableView* table_;             ///< The runs, through proxy_.
    QLabel* details_;               ///< Rich-text details of the selected run.
    /** @brief Opens the selected run; enabled only for a run without load errors. */
    QPushButton* openButton_;
    QWidget* home_;                 ///< The banner and the rows of cards, on a dark ground.
    FeaturedBanner* banner_;        ///< The featured run.
    QVBoxLayout* rows_;             ///< One row of cards per video.
    QList<RunCard*> cards_;         ///< Every card, in row order.
    QList<BundleSummaryPtr> bundles_;  ///< The runs listed.
    ThumbnailCache* thumbnails_ = nullptr;  ///< Makes the stills; null until setThumbnailSource().
};

} // namespace abrviz
