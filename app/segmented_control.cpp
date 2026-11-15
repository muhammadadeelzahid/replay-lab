#include "segmented_control.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QToolButton>

namespace abrviz {

namespace {

/**
 * @brief A colour for a style sheet, as `#AARRGGBB`.
 *
 * @note Fractions in a style sheet's rgba() are not read as alpha, hence the hex form.
 *
 * @param color The colour, alpha included.
 * @return The colour's `#AARRGGBB` name.
 */
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
    // Text tabs, as in a streaming app's top bar: the others grey, the current one white and
    // bold over an accent underline.
    const QColor text = palette().color(QPalette::WindowText);
    QColor muted = palette().color(QPalette::PlaceholderText);
    QColor disabled = muted;
    disabled.setAlphaF(0.35F);
    const QColor accent = palette().color(QPalette::Highlight);
    setStyleSheet(QStringLiteral(R"(
        QWidget#segmentedControl { background: transparent; }
        QWidget#segmentedControl QToolButton {
            border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 4px 14px;
            color: %1; background: transparent; font-size: 14px; font-weight: 500;
        }
        QWidget#segmentedControl QToolButton:hover { color: %2; }
        QWidget#segmentedControl QToolButton:checked { color: %2; font-weight: 700; border-bottom: 2px solid %3; }
        QWidget#segmentedControl QToolButton:disabled { color: %4; }
    )")
                      .arg(muted.name(), text.name(), accent.name(), rgba(disabled)));
}

} // namespace abrviz
