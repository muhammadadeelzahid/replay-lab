#pragma once

/**
 * @file
 * @brief A macOS-style segmented control.
 */

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QToolButton;

namespace abrviz {

/**
 * @brief A macOS-style segmented control: a rounded track with one raised segment selected.
 *
 * Used in the toolbar to switch between the views of a run. Each segment is a checkable tool
 * button in an exclusive group; the colours follow the palette's light or dark appearance.
 */
class SegmentedControl final : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Creates an empty control.
     *
     * @param parent The parent widget.
     */
    explicit SegmentedControl(QWidget* parent = nullptr);

    /**
     * @brief Adds a segment at the end.
     *
     * @param text The segment's label.
     * @param toolTip The segment's tool tip; empty for none.
     * @return The index of the new segment.
     */
    int addSegment(const QString& text, const QString& toolTip = {});

    /**
     * @brief Selects a segment without emitting currentChanged().
     *
     * @param index The segment to select; an index out of range deselects every segment.
     */
    void setCurrentIndex(int index);

    /**
     * @brief The selected segment.
     *
     * @return The index of the selected segment, or -1 if none is selected.
     */
    [[nodiscard]] int currentIndex() const;

    /**
     * @brief Enables or disables one segment.
     *
     * @param index The segment; an index out of range is ignored.
     * @param enabled Whether the segment can be clicked.
     */
    void setSegmentEnabled(int index, bool enabled);

    /**
     * @brief The number of segments.
     *
     * @return The segment count.
     */
    [[nodiscard]] int count() const;

    /**
     * @brief The label of one segment.
     *
     * @param index The segment.
     * @return Its text, or an empty string if `index` is out of range.
     */
    [[nodiscard]] QString segmentText(int index) const;

signals:
    /**
     * @brief Emitted when the user clicks a segment (not on setCurrentIndex()).
     *
     * @param index The clicked segment.
     */
    void currentChanged(int index);

protected:
    /**
     * @brief Restyles the control when the palette changes (light/dark switch).
     *
     * @param event The change event.
     */
    void changeEvent(QEvent* event) override;

private:
    /**
     * @brief Sets the style sheet with AppKit's segmented-control colours for the appearance.
     */
    void applyStyle();
    bool restyling_ = false;  ///< Guards changeEvent(): setting a style sheet changes the palette.

    QButtonGroup* group_;          ///< Makes the buttons exclusive; button ids are segment indices.
    QHBoxLayout* layout_;          ///< Lays the buttons out in a row.
    QList<QToolButton*> buttons_;  ///< One button per segment, in order; children of the control.
};

} // namespace abrviz
