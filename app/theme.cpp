#include "theme.h"

#include <QApplication>
#include <QStyleHints>

namespace abrviz::theme {

namespace {

/// The colours of one appearance, after Apple's system greys.
struct Colors {
    QColor window;     ///< The window's background.
    QColor surface;    ///< Fields, tables and cards.
    QColor alternate;  ///< Alternate table rows.
    QColor raised;     ///< Buttons and controls.
    QColor line;       ///< Borders.
    QColor text;       ///< Main text.
    QColor muted;      ///< Secondary text.
};

Colors colors(bool dark)
{
    if (dark)
        return {QColor(0x1c, 0x1c, 0x1e), QColor(0x24, 0x24, 0x27), QColor(0x29, 0x29, 0x2c),
                QColor(0x3a, 0x3a, 0x3c), QColor(0x3a, 0x3a, 0x3c), QColor(0xf5, 0xf5, 0xf7),
                QColor(0x98, 0x98, 0x9f)};
    return {QColor(0xf5, 0xf5, 0xf7), QColor(0xff, 0xff, 0xff), QColor(0xf7, 0xf7, 0xf9),
            QColor(0xe8, 0xe8, 0xed), QColor(0xd2, 0xd2, 0xd7), QColor(0x1d, 0x1d, 0x1f),
            QColor(0x6e, 0x6e, 0x73)};
}

bool systemIsDark()
{
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void applyAppearance(QApplication& app)
{
    const bool dark = systemIsDark();
    QApplication::setPalette(palette(dark));
    app.setStyleSheet(styleSheet(dark));
}

} // namespace

QColor accent(bool dark)
{
    return dark ? QColor(0xff, 0x6b, 0x4a) : QColor(0xe9, 0x53, 0x2f);
}

QPalette palette(bool dark)
{
    const Colors c = colors(dark);
    QPalette p;
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        const QColor text = disabled ? c.muted : c.text;
        p.setColor(group, QPalette::Window, c.window);
        p.setColor(group, QPalette::WindowText, text);
        p.setColor(group, QPalette::Base, c.surface);
        p.setColor(group, QPalette::AlternateBase, c.alternate);
        p.setColor(group, QPalette::Text, text);
        p.setColor(group, QPalette::Button, c.raised);
        p.setColor(group, QPalette::ButtonText, text);
        p.setColor(group, QPalette::BrightText, dark ? Qt::white : Qt::black);
        p.setColor(group, QPalette::PlaceholderText, c.muted);
        p.setColor(group, QPalette::ToolTipBase, c.surface);
        p.setColor(group, QPalette::ToolTipText, c.text);
        p.setColor(group, QPalette::Highlight, disabled ? c.raised : accent(dark));
        p.setColor(group, QPalette::HighlightedText, Qt::white);
        p.setColor(group, QPalette::Link, accent(dark));
        p.setColor(group, QPalette::Mid, c.raised);
        p.setColor(group, QPalette::Light, c.line);
        p.setColor(group, QPalette::Dark, dark ? QColor(0x14, 0x14, 0x16) : QColor(0xe5, 0xe5, 0xea));
    }
    return p;
}

QString styleSheet(bool dark)
{
    const Colors c = colors(dark);
    const QString scrollHandle = dark ? QStringLiteral("#4a4a4e") : QStringLiteral("#c7c7cc");
    return QStringLiteral(R"(
        QToolTip { color: %1; background: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 8px; }
        QLineEdit { background: %2; color: %1; border: 1px solid %3; border-radius: 8px; padding: 5px 8px; }
        QLineEdit:focus { border: 1px solid %4; }
        QComboBox { background: %2; color: %1; border: 1px solid %3; border-radius: 8px; padding: 4px 10px; }
        QTableView { background: %2; alternate-background-color: %5; color: %1; border: none;
                     border-radius: 10px; selection-background-color: %4; selection-color: white; }
        QHeaderView::section { background: %2; color: %6; border: none; padding: 6px 8px; font-weight: 600; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: %7; border-radius: 4px; min-height: 30px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: %7; border-radius: 4px; min-width: 30px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )")
        .arg(c.text.name(), c.surface.name(), c.line.name(), accent(dark).name(), c.alternate.name(),
             c.muted.name(), scrollHandle);
}

void apply(QApplication& app)
{
    // Follow the system appearance, and switch with it. ABRVIZ_APPEARANCE=light or dark fixes
    // one instead (for screenshots of both).
    const QString forced = qEnvironmentVariable("ABRVIZ_APPEARANCE").toLower();
    if (forced == QLatin1String("light"))
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    else if (forced == QLatin1String("dark"))
        QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    else
        QApplication::styleHints()->unsetColorScheme();
    applyAppearance(app);
    QObject::connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&app] { applyAppearance(app); });
}

} // namespace abrviz::theme
