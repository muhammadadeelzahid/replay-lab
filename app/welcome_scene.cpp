#include "welcome_scene.h"

#include "format.h"
#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace abrviz {

namespace {

constexpr int kTickMs = 33;             ///< Animation interval, in milliseconds.
constexpr double kPassSeconds = 60.0;   ///< How long the satellite takes to cross the sky.

// The scene's colours, the same in light and dark mode.
const QColor kNight(0x14, 0x3a, 0x7d);      ///< The sky overhead.
const QColor kNavy(0x15, 0x2d, 0x57);       ///< Names on the snow.
const QColor kSlate(0x4a, 0x62, 0x88);      ///< Notes on the snow.
const QColor kSteel(0x4b, 0x5f, 0x82);      ///< The mast and the dish's pedestal.
const QColor kLink(0x2f, 0x5f, 0xa8);       ///< Dashes over the snow.

/**
 * @brief Rotates a vector.
 *
 * @param v The vector.
 * @param degrees The angle, clockwise on the screen.
 * @return The rotated vector.
 */
QPointF rotated(QPointF v, double degrees)
{
    const double r = degrees * std::numbers::pi / 180.0;
    return {v.x() * std::cos(r) - v.y() * std::sin(r), v.x() * std::sin(r) + v.y() * std::cos(r)};
}

/**
 * @brief Draws a name and a note under it, centred on a point and kept inside the scene.
 *
 * @param painter The painter.
 * @param anchor The top centre of the name.
 * @param name The name, in bold.
 * @param note The note, smaller, under the name.
 * @param ink The name's colour.
 * @param noteInk The note's colour.
 * @param width The scene's width, to keep the text inside it.
 */
void drawCaption(QPainter& painter, QPointF anchor, const QString& name, const QString& note,
                 const QColor& ink, const QColor& noteInk, double width)
{
    QFont bold = painter.font();
    bold.setBold(true);
    const QFont small = format::scaled(painter.font(), 0.85);
    const double textWidth = std::max(QFontMetricsF(bold).horizontalAdvance(name),
                                      QFontMetricsF(small).horizontalAdvance(note)) + 8.0;
    double left = anchor.x() - textWidth / 2.0;
    left = std::clamp(left, 10.0, std::max(10.0, width - 10.0 - textWidth));
    const double nameHeight = QFontMetricsF(bold).height();
    painter.save();
    painter.setFont(bold);
    painter.setPen(ink);
    painter.drawText(QRectF(left, anchor.y(), textWidth, nameHeight), Qt::AlignCenter, name);
    painter.setFont(small);
    painter.setPen(noteInk);
    painter.drawText(QRectF(left, anchor.y() + nameHeight, textWidth, QFontMetricsF(small).height()),
                     Qt::AlignCenter, note);
    painter.restore();
}

/**
 * @brief Draws a dashed link, white over the sky and blue over the snow.
 *
 * @param painter The painter.
 * @param from Where the link starts.
 * @param to Where it ends.
 * @param horizon The horizon's height, where the colour changes.
 * @param size The scene's size.
 * @param phase The dash offset, which makes the dashes flow from `from` to `to`.
 */
void drawLink(QPainter& painter, QPointF from, QPointF to, double horizon, QSizeF size, double phase)
{
    QPen pen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap);
    pen.setDashPattern({3.0, 3.5});
    pen.setDashOffset(-phase);
    for (const bool above : {true, false}) {
        painter.save();
        painter.setClipRect(above ? QRectF(0, 0, size.width(), horizon)
                                  : QRectF(0, horizon, size.width(), size.height()));
        pen.setColor(above ? QColor(255, 255, 255, 225) : kLink);
        painter.setPen(pen);
        painter.drawLine(from, to);
        painter.restore();
    }
}

} // namespace

WelcomeScene::WelcomeScene(QWidget* parent)
    : QWidget(parent)
    , animation_(new QTimer(this))
{
    setObjectName(QStringLiteral("welcomeScene"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    animation_->setInterval(kTickMs);
    connect(animation_, &QTimer::timeout, this, [this] {
        progress_ = std::fmod(progress_ + kTickMs / 1000.0 / kPassSeconds, 1.0);
        phase_ += 0.5;
        update();
    });
}

QSize WelcomeScene::sizeHint() const
{
    return {520, 760};
}

QSize WelcomeScene::minimumSizeHint() const
{
    return {260, 420};
}

bool WelcomeScene::isAnimating() const
{
    return animation_->isActive();
}

void WelcomeScene::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    animation_->start();
}

void WelcomeScene::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    animation_->stop();
}

void WelcomeScene::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double w = width();
    const double h = height();
    const QSizeF size(w, h);
    const double horizon = std::round(h * 0.68);

    // Sky: deep blue overhead, paler towards the horizon, with a trace of pink at it.
    painter.setPen(Qt::NoPen);
    QLinearGradient dusk(0, 0, 0, horizon);
    dusk.setColorAt(0.0, kNight);
    dusk.setColorAt(0.55, QColor(0x2d, 0x63, 0xb0));
    dusk.setColorAt(0.88, QColor(0x86, 0xab, 0xda));
    dusk.setColorAt(1.0, QColor(0xd8, 0xcf, 0xe2));
    painter.fillRect(QRectF(0, 0, w, horizon + 2.0), dusk);

    // A half moon and a few early stars.
    const QPointF moon(w * 0.26, h * 0.13);
    painter.setBrush(QColor(0xf7, 0xf4, 0xe8));
    painter.drawPie(QRectF(moon.x() - 8.0, moon.y() - 8.0, 16.0, 16.0), 90 * 16, 180 * 16);
    painter.setBrush(QColor(255, 255, 255, 120));
    for (const auto& [x, y] : std::array<std::array<double, 2>, 7>{
             {{0.08, 0.06}, {0.44, 0.04}, {0.70, 0.10}, {0.90, 0.05}, {0.58, 0.20}, {0.12, 0.27},
              {0.86, 0.30}}})
        painter.drawEllipse(QPointF(x * w, y * h), 1.3, 1.3);

    // The satellite's orbit, and the satellite on it, crossing from left to right.
    const QPointF orbitCentre(w * 0.5, h * 0.30);
    const double rx = w * 0.78;
    const double ry = h * 0.08;
    const double tilt = -9.0;
    const auto orbitPoint = [&](double degrees) {
        const double a = degrees * std::numbers::pi / 180.0;
        return orbitCentre + rotated(QPointF(rx * std::cos(a), ry * std::sin(a)), tilt);
    };
    QPainterPath orbit(orbitPoint(180.0));
    for (int d = 182; d <= 360; d += 2)
        orbit.lineTo(orbitPoint(d));
    QPen orbitPen(QColor(255, 255, 255, 60), 1.2);
    orbitPen.setDashPattern({1.0, 5.0});
    orbitPen.setCapStyle(Qt::RoundCap);
    painter.setPen(orbitPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(orbit);
    const QPointF satellite = orbitPoint(195.0 + 150.0 * progress_);

    // Distant hills on the left, in the haze.
    painter.setPen(Qt::NoPen);
    QPainterPath hills(QPointF(0, horizon + 2.0));
    hills.lineTo(0, horizon - h * 0.03);
    hills.quadTo(w * 0.10, horizon - h * 0.05, w * 0.22, horizon - h * 0.02);
    hills.quadTo(w * 0.32, horizon - h * 0.005, w * 0.45, horizon + 2.0);
    hills.closeSubpath();
    painter.setBrush(QColor(0x8c, 0xa6, 0xcb, 210));
    painter.drawPath(hills);

    // Snow: a pale plain, and a drift rising towards the right edge.
    QPainterPath plain(QPointF(0, horizon));
    plain.cubicTo(w * 0.3, horizon - 3.0, w * 0.6, horizon + 4.0, w, horizon - 2.0);
    plain.lineTo(w, h);
    plain.lineTo(0, h);
    plain.closeSubpath();
    QLinearGradient snow(0, horizon, 0, h);
    snow.setColorAt(0.0, QColor(0xd3, 0xdf, 0xf0));
    snow.setColorAt(1.0, QColor(0xf2, 0xf6, 0xfc));
    painter.setBrush(snow);
    painter.drawPath(plain);
    QPainterPath drift(QPointF(w * 0.30, h));
    drift.cubicTo(w * 0.55, h - h * 0.04, w * 0.80, horizon + h * 0.10, w, horizon + h * 0.06);
    drift.lineTo(w, h);
    drift.closeSubpath();
    painter.setBrush(QColor(255, 255, 255, 140));
    painter.drawPath(drift);

    // Where the nodes stand.
    const double dishSize = std::clamp(w * 0.24, 80.0, 140.0);
    const QPointF dishBase(w * 0.76, horizon + h * 0.11);
    const double mastHeight = std::clamp(h * 0.15, 70.0, 130.0);
    const QPointF mastBase(w * 0.42, horizon + h * 0.07);
    const QPointF mastTop = mastBase - QPointF(0, mastHeight);
    const QPointF viewer(w * 0.14, horizon + h * 0.15);
    // The dish faces up and to the left, towards the satellite's pass.
    const double dishTilt = -28.0;
    const QPointF dishCentre = dishBase - QPointF(0, dishSize * 0.55);
    const QPointF feed = dishCentre + rotated(QPointF(0, -dishSize * 0.40), dishTilt);

    // Links: the feeder link from the dish up to the satellite, the backhaul down to the relay,
    // and the 5G access link from the relay to the viewer.
    drawLink(painter, feed, satellite, horizon, size, phase_);
    drawLink(painter, satellite, mastTop, horizon, size, phase_);
    drawLink(painter, mastTop + QPointF(0, 6.0), viewer, horizon, size, phase_);

    // The ground-station dish on its pedestal, behind a fence.
    painter.setPen(Qt::NoPen);
    painter.setBrush(kSteel);
    QPainterPath pedestal(dishBase + QPointF(-dishSize * 0.13, 0));
    pedestal.lineTo(dishBase + QPointF(dishSize * 0.13, 0));
    pedestal.lineTo(dishCentre + QPointF(dishSize * 0.05, dishSize * 0.08));
    pedestal.lineTo(dishCentre + QPointF(-dishSize * 0.05, dishSize * 0.08));
    pedestal.closeSubpath();
    painter.drawPath(pedestal);
    painter.save();
    painter.translate(dishCentre);
    painter.rotate(dishTilt);
    QLinearGradient bowl(0, -dishSize * 0.2, 0, dishSize * 0.2);
    bowl.setColorAt(0, QColor(0xfa, 0xfc, 0xff));
    bowl.setColorAt(1, QColor(0xc4, 0xd2, 0xe6));
    painter.setPen(QPen(QColor(0x6f, 0x86, 0xa8), 1.4));
    painter.setBrush(bowl);
    const QRectF face(-dishSize * 0.5, -dishSize * 0.17, dishSize, dishSize * 0.34);
    painter.drawEllipse(face);
    painter.setPen(QPen(QColor(0x9a, 0xae, 0xca), 1.0));
    for (int i = 0; i < 8; ++i) {  // ribs
        const double a = i * std::numbers::pi / 4.0;
        painter.drawLine(QPointF(0, 0), QPointF(std::cos(a) * dishSize * 0.5, std::sin(a) * dishSize * 0.17));
    }
    painter.setPen(QPen(kSteel, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(-dishSize * 0.3, -dishSize * 0.06), QPointF(0, -dishSize * 0.40));
    painter.drawLine(QPointF(dishSize * 0.3, -dishSize * 0.06), QPointF(0, -dishSize * 0.40));
    painter.setBrush(kSteel);
    painter.drawEllipse(QPointF(0, -dishSize * 0.40), 3.0, 3.0);
    painter.restore();
    painter.setPen(QPen(QColor(0x2b, 0x3a, 0x55, 190), 1.6));
    const double fenceTop = dishBase.y() - 16.0;
    const double fenceLeft = dishBase.x() - dishSize * 0.75;
    painter.drawLine(QPointF(fenceLeft, fenceTop), QPointF(w, fenceTop - 3.0));
    for (double x = fenceLeft; x < w; x += 7.0)
        painter.drawLine(QPointF(x, dishBase.y() + 2.0), QPointF(x, fenceTop - (x - fenceLeft) / (w - fenceLeft) * 3.0));

    // The IAB relay: a lattice mast with its antennas at the top and a warning light.
    painter.setPen(QPen(kSteel, 2.0, Qt::SolidLine, Qt::RoundCap));
    const double spread = mastHeight * 0.13;
    const QPointF footLeft = mastBase - QPointF(spread, 0);
    const QPointF footRight = mastBase + QPointF(spread, 0);
    painter.drawLine(footLeft, mastTop - QPointF(3.0, 0));
    painter.drawLine(footRight, mastTop + QPointF(3.0, 0));
    painter.setPen(QPen(kSteel, 1.1));
    const int braces = 5;
    for (int i = 0; i < braces; ++i) {
        const double t0 = static_cast<double>(i) / braces;
        const double t1 = static_cast<double>(i + 1) / braces;
        const auto leftAt = [&](double t) { return footLeft + (mastTop - QPointF(3.0, 0) - footLeft) * t; };
        const auto rightAt = [&](double t) { return footRight + (mastTop + QPointF(3.0, 0) - footRight) * t; };
        painter.drawLine(leftAt(t0), rightAt(t1));
        painter.drawLine(rightAt(t0), leftAt(t1));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xee, 0xf3, 0xfa));
    painter.drawRoundedRect(QRectF(mastTop.x() - 10.0, mastTop.y() + 6.0, 5.0, 16.0), 1.5, 1.5);
    painter.drawRoundedRect(QRectF(mastTop.x() + 5.0, mastTop.y() + 6.0, 5.0, 16.0), 1.5, 1.5);
    const double blink = 0.5 + 0.5 * std::sin(phase_ * 0.25);
    painter.setBrush(QColor(0xff, 0x45, 0x3a, static_cast<int>(120 + 135 * blink)));
    painter.drawEllipse(mastTop - QPointF(0, 3.0), 2.6, 2.6);

    // The user: a frosted disc with a laptop.
    painter.setPen(QPen(kLink, 1.6));
    painter.setBrush(QColor(255, 255, 255, 220));
    painter.drawEllipse(viewer, 20.0, 20.0);
    nodeIcon(Glyph::Laptop).paint(&painter, QRectF(viewer.x() - 15, viewer.y() - 15, 30, 30).toRect());

    // The satellite: a body with two solar panels, turned along its orbit.
    painter.save();
    painter.translate(satellite);
    painter.rotate(-35.0);
    painter.scale(0.30, 0.30);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xf2, 0xf2, 0xf7));
    painter.drawRoundedRect(QRectF(-30, -36, 60, 72), 12, 12);
    painter.setBrush(QColor(0x0a, 0x84, 0xff));
    painter.drawRoundedRect(QRectF(-118, -26, 74, 52), 7, 7);
    painter.drawRoundedRect(QRectF(44, -26, 74, 52), 7, 7);
    painter.setPen(QPen(QColor(0xf2, 0xf2, 0xf7), 10));
    painter.drawLine(QPointF(-44, 0), QPointF(-30, 0));
    painter.drawLine(QPointF(30, 0), QPointF(44, 0));
    painter.restore();

    // Names: white in the sky, navy on the snow.
    const QColor skyNote(0xdc, 0xe8, 0xf8);
    // Above the satellite, clear of its beams, which go down.
    const QPointF satelliteAnchor = satellite + QPointF(0, -58.0);
    drawCaption(painter, satelliteAnchor, tr("LEO satellite gNB"), tr("550 km up, 28 GHz"),
                Qt::white, skyNote, w);
    drawCaption(painter, dishBase + QPointF(0, 10.0), tr("Video server"),
                tr("Sends the video in segments"), kNavy, kSlate, w);
    drawCaption(painter, mastBase + QPointF(0, 10.0), tr("IAB relay"), tr("Backhauled by satellite"),
                kNavy, kSlate, w);
    drawCaption(painter, viewer + QPointF(0, 26.0), tr("User"), tr("An LLM picks the quality"),
                kNavy, kSlate, w);
}

} // namespace abrviz
