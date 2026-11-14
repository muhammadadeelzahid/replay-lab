#pragma once

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QToolButton;

namespace abrviz {

/// A macOS-style segmented control: a rounded track with one raised segment selected. Used in
/// the toolbar to switch between the views of a run.
class SegmentedControl final : public QWidget {
    Q_OBJECT

public:
    explicit SegmentedControl(QWidget* parent = nullptr);

    /// Adds a segment and returns its index.
    int addSegment(const QString& text, const QString& toolTip = {});
    void setCurrentIndex(int index);
    [[nodiscard]] int currentIndex() const;
    void setSegmentEnabled(int index, bool enabled);
    [[nodiscard]] int count() const;
    [[nodiscard]] QString segmentText(int index) const;

signals:
    void currentChanged(int index);

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyStyle();
    bool restyling_ = false;

    QButtonGroup* group_;
    QHBoxLayout* layout_;
    QList<QToolButton*> buttons_;
};

} // namespace abrviz
