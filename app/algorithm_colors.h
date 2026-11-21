#pragma once

/**
 * @file
 * @brief One colour per ABR algorithm, the same in every view.
 */

#include <QColor>
#include <QPalette>
#include <QString>

namespace abrviz {

/**
 * @brief The colour of an ABR algorithm in panes, charts, the scoreboard and cards.
 *
 * The LLM policy uses the app's accent (coral); RobustMPC is blue, MPC green and BOLA violet.
 * Other algorithms are grey.
 *
 * @param key The algorithm's key (`policy.name`), any case.
 * @param palette The palette of the widget that draws it; dark mode gets lighter shades.
 * @return The colour.
 */
[[nodiscard]] inline QColor algorithmColor(const QString& key, const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    const QString k = key.toLower();
    if (k == QLatin1String("llm"))
        return dark ? QColor(0xff, 0x6b, 0x4a) : QColor(0xe9, 0x53, 0x2f);
    if (k == QLatin1String("robustmpc"))
        return dark ? QColor(0x5a, 0xa2, 0xff) : QColor(0x1f, 0x6f, 0xe0);
    if (k == QLatin1String("mpc"))
        return dark ? QColor(0x3d, 0xd6, 0x8c) : QColor(0x1e, 0x9e, 0x5e);
    if (k == QLatin1String("bola"))
        return dark ? QColor(0xbf, 0x8c, 0xff) : QColor(0x8a, 0x4f, 0xe0);
    return dark ? QColor(0xae, 0xae, 0xb2) : QColor(0x6e, 0x6e, 0x73);
}

} // namespace abrviz
