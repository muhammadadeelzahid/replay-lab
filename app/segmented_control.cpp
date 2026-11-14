#include "segmented_control.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QToolButton>

namespace abrviz {

namespace {

bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

/// A colour for a style sheet: #AARRGGBB (fractions in rgba() are not read as alpha).
QString rgba(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

} // namespace

SegmentedControl::SegmentedControl(QWidget* parent)
    : QWidget(parent)
    , group_(new QButtonGroup(this))
    , layout_(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("segmentedControl"));
    setAttribute(Qt::WA_StyledBackground);
    group_->setExclusive(true);
    layout_->setContentsMargins(2, 2, 2, 2);
    layout_->setSpacing(2);
    connect(group_, &QButtonGroup::idClicked, this, &SegmentedControl::currentChanged);
    applyStyle();
}

int SegmentedControl::addSegment(const QString& text, const QString& toolTip)
{
    auto* button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumWidth(84);
    const int index = static_cast<int>(buttons_.size());
    group_->addButton(button, index);
    layout_->addWidget(button);
    buttons_.append(button);
    return index;
}

void SegmentedControl::setCurrentIndex(int index)
{
    if (index >= 0 && index < buttons_.size()) {
        buttons_[index]->setChecked(true);
        return;
    }
    // No segment: an exclusive group cannot uncheck its last button.
    group_->setExclusive(false);
    for (QToolButton* button : buttons_)
        button->setChecked(false);
    group_->setExclusive(true);
}

int SegmentedControl::currentIndex() const
{
    return group_->checkedId();
}

void SegmentedControl::setSegmentEnabled(int index, bool enabled)
{
    if (index >= 0 && index < buttons_.size())
        buttons_[index]->setEnabled(enabled);
}

int SegmentedControl::count() const
{
    return static_cast<int>(buttons_.size());
}

QString SegmentedControl::segmentText(int index) const
{
    return index >= 0 && index < buttons_.size() ? buttons_[index]->text() : QString();
}

void SegmentedControl::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Setting a style sheet changes the palette too, so the restyle must not re-enter itself.
    if (event->type() == QEvent::PaletteChange && !restyling_) {
        restyling_ = true;
        applyStyle();
        restyling_ = false;
    }
}

void SegmentedControl::applyStyle()
{
    // The colours of AppKit's segmented control, in both appearances.
    const bool dark = isDark(palette());
    const QColor track = dark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 15);
    const QColor selected = dark ? QColor(255, 255, 255, 56) : QColor(255, 255, 255);
    const QColor border = dark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 30);
    const QColor text = palette().color(QPalette::WindowText);
    QColor disabled = text;
    disabled.setAlphaF(0.3F);
    setStyleSheet(QStringLiteral(R"(
        QWidget#segmentedControl { background: %1; border-radius: 7px; }
        QWidget#segmentedControl QToolButton {
            border: none; border-radius: 5px; padding: 3px 14px;
            color: %4; background: transparent; font-size: 13px;
        }
        QWidget#segmentedControl QToolButton:checked { background: %2; border: 0.5px solid %3; }
        QWidget#segmentedControl QToolButton:disabled { color: %5; }
    )")
                      .arg(rgba(track), rgba(selected), rgba(border), text.name(), rgba(disabled)));
}

} // namespace abrviz
