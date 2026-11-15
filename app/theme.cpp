#include "theme.h"

#include <QApplication>
#include <QStyleHints>

namespace abrviz::theme {

QPalette palette()
{
    QPalette p;
    for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool disabled = group == QPalette::Disabled;
        const QColor text = disabled ? kMuted.darker(130) : kText;
        p.setColor(group, QPalette::Window, kBackground);
        p.setColor(group, QPalette::WindowText, text);
        p.setColor(group, QPalette::Base, kSurface);
        p.setColor(group, QPalette::AlternateBase, QColor(0x17, 0x1c, 0x28));
        p.setColor(group, QPalette::Text, text);
        p.setColor(group, QPalette::Button, kRaised);
        p.setColor(group, QPalette::ButtonText, text);
        p.setColor(group, QPalette::BrightText, Qt::white);
        p.setColor(group, QPalette::PlaceholderText, kMuted);
        p.setColor(group, QPalette::ToolTipBase, kRaised);
        p.setColor(group, QPalette::ToolTipText, kText);
        p.setColor(group, QPalette::Highlight, disabled ? kRaised : kAccent);
        p.setColor(group, QPalette::HighlightedText, Qt::white);
        p.setColor(group, QPalette::Link, kAccent.lighter(120));
        p.setColor(group, QPalette::Mid, kRaised);
        p.setColor(group, QPalette::Dark, kSidebar);
        p.setColor(group, QPalette::Light, QColor(0x2a, 0x31, 0x44));
    }
    return p;
}

void apply(QApplication& app)
{
    // Native controls, menus and the title bar follow the colour scheme.
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QApplication::setPalette(palette());
    app.setStyleSheet(QStringLiteral(R"(
        QToolTip { color: #f4f6fb; background: #1c2231; border: 1px solid #2a3144; border-radius: 6px; padding: 4px 8px; }
        QLineEdit { background: #1c2231; color: #f4f6fb; border: 1px solid #2a3144; border-radius: 8px; padding: 5px 8px; }
        QLineEdit:focus { border: 1px solid #2f7bff; }
        QComboBox { background: #1c2231; color: #f4f6fb; border: 1px solid #2a3144; border-radius: 8px; padding: 4px 10px; }
        QTableView { background: #131722; alternate-background-color: #171c28; color: #f4f6fb;
                     border: none; border-radius: 10px; selection-background-color: #2f7bff; }
        QHeaderView::section { background: #131722; color: #8d96ab; border: none; padding: 6px 8px; font-weight: 600; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #2a3144; border-radius: 4px; min-height: 30px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: #2a3144; border-radius: 4px; min-width: 30px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )"));
}

} // namespace abrviz::theme
