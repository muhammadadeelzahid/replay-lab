#include "icons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace abrviz {

namespace {

/**
 * @brief The body of a 24 x 24 SVG for a glyph.
 *
 * `C` stands for the colour, which the caller substitutes. Strokes are 1.8 wide and round
 * (set on the enclosing `<svg>` element by the caller).
 *
 * @param glyph The icon.
 * @return The SVG elements without the `<svg>` element; empty for an unknown glyph.
 */
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
    case Glyph::Film:
        return QStringLiteral(R"(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/>
            <path d="M10 9.2v5.6l4.6-2.8z" fill="C"/>)");
    case Glyph::Inspector:
        return QStringLiteral(R"(<rect x="3" y="4.5" width="18" height="15" rx="2.5"/><path d="M14.5 4.5v15"/>
            <path d="M16.8 8.5h1.7M16.8 11h1.7M16.8 13.5h1.7"/>)");
    case Glyph::Sidebar:
        return QStringLiteral(R"(<rect x="3" y="4.5" width="18" height="15" rx="2.5"/><path d="M9.5 4.5v15"/>
            <path d="M5.5 8.5h1.7M5.5 11h1.7M5.5 13.5h1.7"/>)");
    case Glyph::Settings:
        return QStringLiteral(R"(<circle cx="12" cy="12" r="3.2"/>
            <path d="M12 2.8v2.6M12 18.6v2.6M2.8 12h2.6M18.6 12h2.6M5.5 5.5l1.9 1.9M16.6 16.6l1.9 1.9M5.5 18.5l1.9-1.9M16.6 7.4l1.9-1.9"/>)");
    case Glyph::Satellite:
        return QStringLiteral(R"svg(<g transform="rotate(-45 12 12)">
            <rect x="9.6" y="8.8" width="4.8" height="6.4" rx="1.1"/>
            <rect x="2.4" y="9.6" width="5.6" height="4.8" rx="0.7"/><path d="M5.2 9.6v4.8"/>
            <rect x="16" y="9.6" width="5.6" height="4.8" rx="0.7"/><path d="M18.8 9.6v4.8"/>
            <path d="M8 12h1.6M14.4 12H16"/><path d="M12 15.2v2.2"/></g>
            <path d="M4.2 19.8a4.5 4.5 0 0 0 4.2-1.9"/><path d="M2.6 16.6a2.6 2.6 0 0 0 2.6-1.1"/>)svg");
    case Glyph::Relay:
        return QStringLiteral(R"(<path d="M12 10.4v10.1"/><path d="M8.6 20.5h6.8"/><path d="M9.6 20.5L12 13l2.4 7.5"/>
            <circle cx="12" cy="8.6" r="1.5" fill="C"/>
            <path d="M8.8 5.6a4.6 4.6 0 0 0 0 6"/><path d="M15.2 5.6a4.6 4.6 0 0 1 0 6"/>
            <path d="M6.2 3.4a8 8 0 0 0 0 10.4"/><path d="M17.8 3.4a8 8 0 0 1 0 10.4"/>)");
    case Glyph::Server:
        return QStringLiteral(R"(<rect x="4" y="4" width="16" height="6.6" rx="1.6"/>
            <rect x="4" y="13.4" width="16" height="6.6" rx="1.6"/>
            <circle cx="7.6" cy="7.3" r="0.9" fill="C"/><circle cx="7.6" cy="16.7" r="0.9" fill="C"/>
            <path d="M11.5 7.3h5M11.5 16.7h5"/>)");
    case Glyph::Device:
        return QStringLiteral(R"(<rect x="7" y="2.6" width="10" height="18.8" rx="2.2"/><path d="M10.6 18.4h2.8"/>)");
    case Glyph::Laptop:
        return QStringLiteral(R"(<rect x="4.5" y="5" width="15" height="10.5" rx="1.6"/><path d="M2.2 18.6h19.6"/>)");
    case Glyph::Download:
        return QStringLiteral(R"(<path d="M12 3.5v11"/><path d="M7.5 10.5l4.5 4.5 4.5-4.5"/>
            <path d="M4.5 15.5v2.6a2 2 0 0 0 2 2h11a2 2 0 0 0 2-2v-2.6"/>)");
    case Glyph::Chevron:
        return QStringLiteral(R"(<path d="M9.5 6l6 6-6 6"/>)");
    case Glyph::Search:
        return QStringLiteral(R"(<circle cx="10.5" cy="10.5" r="6"/><path d="M15 15l5 5"/>)");
    case Glyph::Overlay:
        return QStringLiteral(R"(<rect x="3" y="4.5" width="18" height="15" rx="2"/>
            <rect x="5.5" y="7" width="7.5" height="4" rx="0.8" fill="C"/>)");
    }
    return {};
}

/**
 * @brief An icon engine that renders a glyph's SVG at whatever size is asked for.
 *
 * Rendering at the requested size keeps the icon sharp on Retina screens.
 */
class GlyphEngine final : public QIconEngine {
public:
    /**
     * @brief Creates an engine for one glyph in the given colours.
     *
     * @param glyph The icon to draw.
     * @param color The normal colour.
     * @param selected The colour for QIcon::Selected; ignored if invalid.
     */
    GlyphEngine(Glyph glyph, QColor color, QColor selected)
        : glyph_(glyph), color_(std::move(color)), selected_(std::move(selected))
    {
    }

    /**
     * @brief Renders the glyph into `rect`.
     *
     * Uses the selected colour in QIcon::Selected mode (if valid) and 35% alpha in
     * QIcon::Disabled mode. SVG Tiny has no `#AARRGGBB` colours, so the colour's alpha is applied
     * as painter opacity.
     *
     * @param painter The painter to draw with; its state is restored afterwards.
     * @param rect The area to fill with the 24 x 24 view box.
     * @param mode The icon mode, which picks the colour.
     */
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

    /**
     * @brief Renders the glyph into a pixmap at a device pixel ratio of 1.
     *
     * @param size The pixmap size in pixels.
     * @param mode The icon mode, which picks the colour.
     * @param state The icon state (the glyph looks the same in both).
     * @return A transparent pixmap with the glyph drawn on it.
     * @see scaledPixmap()
     */
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    /**
     * @brief Renders the glyph into a pixmap for a given device pixel ratio.
     *
     * @param size The pixmap size in device-independent pixels.
     * @param mode The icon mode, which picks the colour.
     * @param state The icon state (the glyph looks the same in both).
     * @param scale The device pixel ratio; the pixmap has `size * scale` physical pixels.
     * @return A transparent, antialiased pixmap with the glyph drawn on it.
     */
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

    /**
     * @brief Copies the engine, as QIcon does when it detaches.
     *
     * @return A new engine with the same glyph and colours; the caller owns it.
     */
    [[nodiscard]] QIconEngine* clone() const override { return new GlyphEngine(glyph_, color_, selected_); }

private:
    Glyph glyph_;      ///< The icon to draw.
    QColor color_;     ///< Normal colour; alpha is applied as painter opacity.
    QColor selected_;  ///< Colour for selected items; invalid means use color_.
};

/**
 * @brief A node of the network path as an app-style tile: the node's line symbol in white on a
 *        rounded square with a gradient of the node's colour, as in System Settings.
 *
 * @param glyph Glyph::Server (graphite), Glyph::Satellite (blue), Glyph::Relay (green) or
 *              Glyph::Laptop / Glyph::Device (indigo).
 * @return The SVG document in a 48 x 48 view box; empty for other glyphs.
 */
QString nodeSvg(Glyph glyph)
{
    QString top;
    QString bottom;
    switch (glyph) {
    case Glyph::Server:
        top = QStringLiteral("#98989f");
        bottom = QStringLiteral("#4a4a50");
        break;
    case Glyph::Satellite:
        top = QStringLiteral("#3d9bff");
        bottom = QStringLiteral("#0a56d9");
        break;
    case Glyph::Relay:
        top = QStringLiteral("#3ddc6a");
        bottom = QStringLiteral("#1f9a45");
        break;
    case Glyph::Device:
    case Glyph::Laptop:
        top = QStringLiteral("#7a78ff");
        bottom = QStringLiteral("#4643d6");
        glyph = Glyph::Laptop;
        break;
    default:
        return {};
    }
    QString body = glyphBody(glyph);
    body.replace(QStringLiteral("\"C\""), QStringLiteral("\"#ffffff\""));
    return QStringLiteral(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
  <defs><linearGradient id="t" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="%1"/><stop offset="1" stop-color="%2"/></linearGradient></defs>
  <rect x="1" y="1" width="46" height="46" rx="11" fill="url(#t)"/>
  <rect x="1.5" y="1.5" width="45" height="45" rx="10.5" fill="none" stroke="#ffffff" stroke-opacity="0.18"/>
  <g transform="translate(9 9) scale(1.25)" fill="none" stroke="#ffffff" stroke-width="1.7"
     stroke-linecap="round" stroke-linejoin="round">%3</g>
</svg>)svg")
        .arg(top, bottom, body);
}

/**
 * @brief An icon engine that renders a full-colour SVG at whatever size is asked for.
 */
class SvgEngine final : public QIconEngine {
public:
    /// @param svg The SVG document.
    explicit SvgEngine(QString svg) : svg_(std::move(svg)) {}

    /**
     * @brief Renders the drawing into `rect`, at 40% opacity in QIcon::Disabled mode.
     *
     * @param painter The painter; its state is restored afterwards.
     * @param rect The area to fill.
     * @param mode The icon mode.
     */
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        QSvgRenderer renderer(svg_.toUtf8());
        painter->save();
        if (mode == QIcon::Disabled)
            painter->setOpacity(painter->opacity() * 0.4);
        renderer.render(painter, rect);
        painter->restore();
    }

    /// @return The drawing in a pixmap of `size` at a device pixel ratio of 1.
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    /// @return The drawing in a pixmap of `size` for the device pixel ratio `scale`.
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

    /// @return A copy of the engine; the caller owns it.
    [[nodiscard]] QIconEngine* clone() const override { return new SvgEngine(svg_); }

private:
    QString svg_;  ///< The SVG document.
};

} // namespace

QIcon glyphIcon(Glyph glyph, const QColor& color, const QColor& selected)
{
    return QIcon(new GlyphEngine(glyph, color, selected));  // QIcon takes ownership of the engine
}

QIcon nodeIcon(Glyph glyph)
{
    const QString svg = nodeSvg(glyph);
    if (svg.isEmpty())
        return glyphIcon(glyph, Qt::white);
    return QIcon(new SvgEngine(svg));  // QIcon takes ownership of the engine
}

QColor accentColor(const QPalette& palette)
{
    // The coral of the app icon's play symbol, a little lighter in dark mode.
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    return dark ? QColor(0xff, 0x6b, 0x4a) : QColor(0xe9, 0x53, 0x2f);
}

QIcon appIcon()
{
    return QIcon(QStringLiteral(":/abrviz/app-icon.svg"));
}

} // namespace abrviz
