#include "network_path_view.h"

#include "format.h"
#include "icons.h"
#include "replay_player.h"
#include "session.h"
#include "time_cursor.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <array>

namespace abrviz {

namespace {

constexpr int kHeight = 184;          ///< Fixed height of the strip, in pixels.
constexpr double kPadding = 14.0;     ///< Space between the drawing and the edges of the strip.
constexpr double kLabelHeight = 20.0; ///< Height of a node's name.
constexpr double kNodeRadius = 21.0;  ///< Radius of a node's disc, in pixels.

/**
 * @brief A node of the path as painted: a disc with a glyph and a name.
 */
struct Node {
    Glyph glyph;     ///< Icon drawn in the disc.
    QString title;   ///< Name drawn under the disc.
    QPointF centre;  ///< Centre of the disc, in widget coordinates.
};

/**
 * @brief A link between two nodes as painted: a curve with its name at the middle.
 */
struct Link {
    QString label;  ///< Name drawn at the middle of the link.
    /** @brief Curve in widget coordinates; for the access link only its start, at the relay. */
    QPainterPath path;
};

/**
 * @brief A distance in whole kilometres, e.g. "550 km".
 *
 * @param metres The distance in metres.
 * @return The distance in kilometres, rounded, with the unit.
 */
QString kilometres(double metres)
{
    return QStringLiteral("%1 km").arg(metres / 1000.0, 0, 'f', 0);
}

/**
 * @brief Whether a palette is a dark appearance.
 *
 * @param palette The palette to test.
 * @return True if the window colour's lightness is below 128.
 */
bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

} // namespace

NetworkPathView::NetworkPathView(Session* session, ReplayPlayer* player, QWidget* parent)
    : QWidget(parent)
    , session_(session)
    , player_(player)
    , animation_(new QTimer(this))
{
    setObjectName(QStringLiteral("networkPath"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    animation_->setInterval(33);
    connect(animation_, &QTimer::timeout, this, [this] {
        phase_ += 1.4;
        update();
    });
    if (session_) {
        connect(session_, &Session::runChanged, this, &NetworkPathView::refresh);
        connect(session_->cursor(), &TimeCursor::timeChanged, this, &NetworkPathView::refresh);
    }
    if (player_)
        connect(player_, &ReplayPlayer::stateChanged, this, &NetworkPathView::refresh);
    refresh();
}

QSize NetworkPathView::sizeHint() const
{
    return {900, kHeight};
}

QSize NetworkPathView::minimumSizeHint() const
{
    return {520, kHeight};
}

void NetworkPathView::refresh()
{
    run_ = session_ ? session_->run() : nullptr;
    downloading_ = false;
    stalled_ = false;
    running_ = player_ && player_->isRunning();
    activity_.clear();
    if (run_) {
        const double time = session_->cursor()->time();
        const PlaybackClock::Position position = session_->clock().positionAtSim(time);
        stalled_ = position.stalled;
        // The newest segment whose download spans the cursor.
        const Segment* active = nullptr;
        for (const Segment& segment : run_->segments) {
            if (segment.requestTime <= time && time < segment.doneTime)
                active = &segment;
        }
        downloading_ = active != nullptr;
        if (active) {
            activity_ = tr("Downloading segment %1 at %2 over the satellite")
                            .arg(active->index)
                            .arg(format::rungLabel(run_->video, active->playedRung));
        } else if (position.beforeStart) {
            activity_ = tr("Starting the stream");
        } else if (position.afterEnd) {
            activity_ = run_->playbackCutOff() ? tr("The simulation stopped during playback")
                                               : tr("Playback finished");
        } else {
            activity_ = tr("Playing from the buffer; nothing in flight");
        }
        if (stalled_)
            activity_ = tr("Stalled: the buffer ran dry waiting for the satellite link");
    }
    updateAnimation();
    update();
}

void NetworkPathView::updateAnimation()
{
    if (isVisible() && downloading_ && running_)
        animation_->start();
    else
        animation_->stop();
}

void NetworkPathView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateAnimation();
}

void NetworkPathView::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    animation_->stop();
}

QStringList NetworkPathView::labels() const
{
    const NetworkTopology* topology = run_ ? &run_->topology : nullptr;
    QStringList list;
    list << tr("Video server");
    QString feeder = tr("Feeder link");
    if (run_ && run_->info.feederLinkRate)
        feeder += QStringLiteral(" · %1, shared by %2 users")
                      .arg(format::bitrate(static_cast<double>(*run_->info.feederLinkRate)))
                      .arg(run_->info.simulatedUes);
    list << feeder;
    QString satellite = tr("LEO satellite gNB");
    if (topology && topology->satelliteAltitude)
        satellite += QStringLiteral(" · %1").arg(kilometres(*topology->satelliteAltitude));
    list << satellite;
    QString backhaul = tr("Satellite backhaul");
    if (topology && topology->carrierFrequencyGHz)
        backhaul += QStringLiteral(" · %1 GHz").arg(*topology->carrierFrequencyGHz, 0, 'g', 3);
    list << backhaul;
    list << tr("IAB relay");
    QString access = tr("5G access");
    if (topology && topology->accessDistance)
        access += QStringLiteral(" · %1 m").arg(*topology->accessDistance, 0, 'f', 0);
    list << access;
    list << tr("User");
    return list;
}

void NetworkPathView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const bool dark = isDark(pal);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor secondary = pal.color(QPalette::PlaceholderText);
    const QColor accent = accentColor(pal);
    const QColor red(0xff, 0x45, 0x3a);
    QColor idle = text;
    idle.setAlphaF(0.28F);

    // Background: a quiet band, slightly set off from the window.
    QColor band = dark ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 8);
    painter.fillRect(rect(), band);

    const QStringList label = labels();
    const QFont base = font();
    QFont small = format::scaled(base, 0.85);
    QFont bold = base;
    bold.setBold(true);
    const double w = width();
    // The satellite's name sits above its disc and the other names below theirs; the padding
    // keeps all of them clear of the edges.
    const double sky = kPadding + kLabelHeight + 4.0 + kNodeRadius;
    const double ground = height() - kPadding - kLabelHeight - 4.0 - kNodeRadius;
    const double left = 80.0;
    const double right = w - 80.0;
    const std::array<Node, 4> nodes{{
        {Glyph::Server, label[0], {left, ground}},
        {Glyph::Satellite, label[2], {left + (right - left) * 0.34, sky}},
        {Glyph::Relay, label[4], {left + (right - left) * 0.70, ground}},
        {Glyph::Laptop, label[6], {right, ground}},
    }};

    // Links: up to the satellite, down to the relay, across to the viewer.
    auto curve = [](QPointF from, QPointF to, double lift) {
        QPainterPath path(from);
        const QPointF control((from.x() + to.x()) / 2.0, std::min(from.y(), to.y()) - lift);
        path.quadTo(control, to);
        return path;
    };
    const std::array<Link, 3> links{{
        {label[1], curve(nodes[0].centre, nodes[1].centre, 10.0)},
        {label[3], curve(nodes[1].centre, nodes[2].centre, 10.0)},
        {label[5], QPainterPath(nodes[2].centre)},
    }};
    QPainterPath access(nodes[2].centre);
    access.lineTo(nodes[3].centre);

    const bool active = run_ && downloading_;
    for (std::size_t i = 0; i < links.size(); ++i) {
        const QPainterPath& path = i == 2 ? access : links[i].path;
        QPen pen(active ? accent : idle, active ? 2.6 : 1.6);
        pen.setCapStyle(Qt::RoundCap);
        if (active) {
            pen.setDashPattern({3.0, 3.0});
            pen.setDashOffset(-phase_);  // moves from the server towards the viewer
        }
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // The link's name at its middle: under the two satellite links (inside the arc, where
        // there is room), over the access link.
        const QPointF middle = path.pointAtPercent(0.5);
        painter.setFont(small);
        const QFontMetricsF metrics(small);
        const double textWidth = metrics.horizontalAdvance(links[i].label) + 12.0;
        QRectF box(middle.x() - textWidth / 2.0, middle.y() + 8.0, textWidth, metrics.height() + 4.0);
        if (i == 2)
            box.moveTop(middle.y() - metrics.height() - 12.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pal.color(QPalette::Window));
        painter.drawRoundedRect(box, 5, 5);
        painter.setPen(active ? accent : secondary);
        painter.drawText(box, Qt::AlignCenter, links[i].label);
    }

    // Nodes: a disc with the glyph, the name underneath (above it for the satellite).
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        const bool viewer = i == 3;
        QColor fill = viewer && stalled_ ? red : (dark ? QColor(255, 255, 255) : QColor(0x1c, 0x2a, 0x48));
        fill.setAlphaF(viewer && stalled_ ? 0.25F : (dark ? 0.08F : 0.85F));
        QColor ring = viewer && stalled_ ? red : (run_ ? accent : idle);
        painter.setPen(QPen(ring, 1.6));
        painter.setBrush(fill);
        painter.drawEllipse(node.centre, kNodeRadius, kNodeRadius);
        // The node in full colour, a little larger than the disc's inside.
        const QRectF iconRect(node.centre.x() - 15, node.centre.y() - 15, 30, 30);
        nodeIcon(node.glyph).paint(&painter, iconRect.toRect());

        painter.setFont(bold);
        painter.setPen(viewer && stalled_ ? red : text);
        const double labelTop = i == 1 ? node.centre.y() - kNodeRadius - 4.0 - kLabelHeight
                                       : node.centre.y() + kNodeRadius + 4.0;
        const QRectF labelRect(node.centre.x() - 150.0, labelTop, 300.0, kLabelHeight);
        painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         viewer && stalled_ ? tr("User · stalled") : node.title);
    }

    // What is happening at the cursor, top right.
    if (!activity_.isEmpty()) {
        painter.setFont(base);
        const QFontMetricsF metrics(base);
        const double textWidth = std::min(metrics.horizontalAdvance(activity_) + 4.0, w * 0.42);
        const QRectF box(w - kPadding - textWidth, kPadding, textWidth, kLabelHeight);
        const QColor dot = stalled_ ? red : (downloading_ ? accent : secondary);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(box.left() - 10, box.center().y()), 4, 4);
        painter.setPen(stalled_ ? red : secondary);
        painter.drawText(box, Qt::AlignRight | Qt::AlignVCenter,
                         metrics.elidedText(activity_, Qt::ElideRight, box.width()));
    }
}

} // namespace abrviz
