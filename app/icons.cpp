#include "icons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace abrviz {

namespace {

/// The body of a 24 x 24 SVG; `C` stands for the colour. Strokes are 1.8 wide and round.
QString glyphBody(Glyph glyph)
{
    switch (glyph) {
    case Glyph::Library:
        return QStringLiteral(R"(<rect x="3.5" y="4" width="17" height="4.2" rx="1.2"/>
            <rect x="3.5" y="9.9" width="17" height="4.2" rx="1.2"/>
            <rect x="3.5" y="15.8" width="17" height="4.2" rx="1.2"/>)");
    case Glyph::Replay:
        return QStringLiteral(R"(<rect x="2.5" y="4" width="19" height="13" rx="2"/>
            <path d="M10 8.2v5.6l4.8-2.8z" fill="C"/><path d="M8 20.5h8"/>)");
    case Glyph::Analysis:
        return QStringLiteral(R"(<path d="M3.5 3.5v17h17"/><path d="M7.5 15.5l3.8-4.6 3 2.8 5.2-6.7"/>)");
    case Glyph::Logs:
        return QStringLiteral(R"(<rect x="3.5" y="3.5" width="17" height="17" rx="2"/>
            <path d="M7.5 8.5l2.5 2-2.5 2"/><path d="M12 14.5h4.5"/>)");
    case Glyph::Report:
        return QStringLiteral(R"(<rect x="5" y="4.5" width="14" height="16" rx="2"/>
            <path d="M9 4.5V3h6v1.5"/><path d="M8.5 13.2l2.3 2.3 4.7-5"/>)");
    case Glyph::OpenFolder:
        return QStringLiteral(R"(<path d="M3.5 7.5a2 2 0 0 1 2-2h3.8l2 2h7.2a2 2 0 0 1 2 2v7.5a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2z"/>)");
    case Glyph::OpenBundle:
        return QStringLiteral(R"(<path d="M6.5 3.5h7.5l4 4v13h-11.5z"/><path d="M13.5 3.5v4.5h4.5"/>
            <path d="M10 12.2v4.6l3.8-2.3z" fill="C"/>)");
    case Glyph::Play:
        return QStringLiteral(R"(<path d="M7.5 5v14l11.5-7z" fill="C"/>)");
    case Glyph::Pause:
        return QStringLiteral(R"(<rect x="6.5" y="5" width="3.6" height="14" rx="1" fill="C"/>
            <rect x="13.9" y="5" width="3.6" height="14" rx="1" fill="C"/>)");
    case Glyph::StepBack:
        return QStringLiteral(R"(<path d="M11.5 7l-5 5 5 5"/><path d="M18 7l-5 5 5 5"/>)");
    case Glyph::StepForward:
        return QStringLiteral(R"(<path d="M12.5 7l5 5-5 5"/><path d="M6 7l5 5-5 5"/>)");
    case Glyph::PreviousSegment:
        return QStringLiteral(R"(<path d="M6.5 5.5v13"/><path d="M18 5.5v13l-9-6.5z" fill="C"/>)");
    case Glyph::NextSegment:
        return QStringLiteral(R"(<path d="M17.5 5.5v13"/><path d="M6 5.5v13l9-6.5z" fill="C"/>)");
    case Glyph::Render:
        return QStringLiteral(R"(<rect x="3.5" y="5" width="17" height="14" rx="2"/>
            <path d="M7.5 5v14M16.5 5v14M3.5 9.5h4M3.5 14.5h4M16.5 9.5h4M16.5 14.5h4"/>)");
    case Glyph::Engine:
        return QStringLiteral(R"(<circle cx="12" cy="12" r="3"/>
            <path d="M12 3.5v2.5M12 18v2.5M3.5 12h2.5M18 12h2.5M6 6l1.8 1.8M16.2 16.2L18 18M6 18l1.8-1.8M16.2 7.8L18 6"/>)");
    case Glyph::Overlay:
        return QStringLiteral(R"(<rect x="3" y="4.5" width="18" height="15" rx="2"/>
            <rect x="5.5" y="7" width="7.5" height="4" rx="0.8" fill="C"/>)");
    }
    return {};
}

/// Renders the SVG at whatever size is asked for, so the icon stays sharp on Retina screens.
class GlyphEngine final : public QIconEngine {
public:
    GlyphEngine(Glyph glyph, QColor color, QColor selected)
        : glyph_(glyph), color_(std::move(color)), selected_(std::move(selected))
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor color = mode == QIcon::Selected && selected_.isValid() ? selected_ : color_;
        if (mode == QIcon::Disabled)
            color.setAlphaF(0.35F);
        // SVG Tiny (what QtSvg reads) has no #AARRGGBB colours: the alpha goes to the painter.
        const QString name = color.name(QColor::HexRgb);
        QString body = glyphBody(glyph_);
        body.replace(QStringLiteral("\"C\""), QStringLiteral("\"%1\"").arg(name));
        const QString svg = QStringLiteral(
            R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="%1" )"
            R"(stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">%2</svg>)")
                                .arg(name, body);
        QSvgRenderer renderer(svg.toUtf8());
        painter->save();
        painter->setOpacity(painter->opacity() * color.alphaF());
        renderer.render(painter, rect);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new GlyphEngine(glyph_, color_, selected_); }

private:
    Glyph glyph_;
    QColor color_;
    QColor selected_;
};

} // namespace

QIcon glyphIcon(Glyph glyph, const QColor& color, const QColor& selected)
{
    return QIcon(new GlyphEngine(glyph, color, selected));  // QIcon takes ownership of the engine
}

QIcon appIcon()
{
    return QIcon(QStringLiteral(":/abrviz/abrviz.svg"));
}

} // namespace abrviz
