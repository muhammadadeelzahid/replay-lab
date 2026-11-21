#pragma once

/**
 * @file
 * @brief Keeps a combo box's open list wide enough for its items.
 */

#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>

#include <algorithm>

namespace abrviz {

/**
 * @brief Makes a combo box's list at least as wide as its longest item.
 *
 * With the app's style sheet the list is drawn by Qt, not by the system, and is only as wide as
 * the closed box; items longer than the box's text ran past its edge. Call it after the items
 * change.
 *
 * @param combo The combo box.
 */
inline void fitComboPopup(QComboBox* combo)
{
    const QFontMetrics metrics(combo->view()->font());
    int widest = 0;
    for (int i = 0; i < combo->count(); ++i)
        widest = std::max(widest, metrics.horizontalAdvance(combo->itemText(i)));
    // Room for the check mark on the left, the padding on both sides and a scroll bar.
    constexpr int kExtra = 64;
    combo->view()->setMinimumWidth(std::max(combo->width(), widest + kExtra));
}

} // namespace abrviz
