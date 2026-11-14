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
constexpr int kHeaderHeight = 26;     ///< Height of the static diagram's heading.
constexpr int kNoteHeight = 26;       ///< Height of the notes under the static diagram's nodes.
constexpr double kCardRadius = 14.0;  ///< Corner radius of the static diagram's card.

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
        phase_ += isStaticDiagram() ? 0.5 : 1.4;  // the static diagram drifts slowly
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
    return {900, isStaticDiagram() ? kHeight + kHeaderHeight + kNoteHeight + 18 : kHeight};
}

QSize NetworkPathView::minimumSizeHint() const
{
    return {520, sizeHint().height()};
}

bool NetworkPathView::isAnimating() const
{
    return animation_->isActive();
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
    if (isVisible() && (isStaticDiagram() || (downloading_ && running_)))
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
        feeder += QStringLiteral(" · %1, shared by %2 UEs")
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
    list << (run_ ? tr("Viewer · UE %1").arg(run_->info.nodeId) : tr("Viewer"));
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

    const bool card = isStaticDiagram();
    const double w = width();
    // The static diagram leaves room for its heading above and the notes below the nodes.
    const double top = card ? kPadding + kHeaderHeight : kPadding;
    const double bottom = height() - kPadding - (card ? kNoteHeight : 0);
    // The static diagram's satellite has a note under its name, above the disc.
    const double sky = top + kLabelHeight + (card ? 18.0 : 0.0) + 4.0 + kNodeRadius;
    const double ground = bottom - kLabelHeight - 4.0 - kNodeRadius;

    if (card) {
        // A card with a night sky over the Earth's horizon; the satellite is in the sky, the
        // other nodes stand on the ground.
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath outline;
        outline.addRoundedRect(frame, kCardRadius, kCardRadius);
        QLinearGradient skyFill(0, 0, 0, height());
        skyFill.setColorAt(0, dark ? QColor(0x17, 0x2a, 0x52) : QColor(0xe6, 0xee, 0xfb));
        skyFill.setColorAt(1, dark ? QColor(0x10, 0x14, 0x22) : QColor(0xf6, 0xf7, 0xfa));
        painter.fillPath(outline, skyFill);
        painter.save();
        painter.setClipPath(outline);
        // Stars, at fixed places in the upper half.
        painter.setPen(Qt::NoPen);
        QColor star = dark ? QColor(255, 255, 255) : accent;
        const std::array<std::array<double, 3>, 9> stars{{{0.06, 0.30, 1.6}, {0.14, 0.62, 1.2},
            {0.22, 0.50, 1.4}, {0.45, 0.46, 1.1}, {0.58, 0.18, 1.6}, {0.66, 0.40, 1.2},
            {0.79, 0.27, 1.5}, {0.88, 0.55, 1.1}, {0.95, 0.20, 1.3}}};
        for (const auto& [x, y, r] : stars) {
            star.setAlphaF(dark ? 0.45F : 0.25F);
            painter.setBrush(star);
            painter.drawEllipse(QPointF(x * w, top + y * (ground - top)), r, r);
        }
        // The horizon: a shallow arc of a large circle, its top just under the ground nodes.
        const double radius = w * 12.0;
        const QPointF earth(w / 2.0, ground + kNodeRadius + 1.0 + radius);
        QRadialGradient land(earth, radius);
        QColor glow = accent;
        glow.setAlphaF(dark ? 0.16F : 0.10F);
        QColor deep = accent;
        deep.setAlphaF(0.0F);
        land.setColorAt(1.0, glow);
        land.setColorAt(1.0 - 60.0 / radius, deep);
        painter.setBrush(land);
        painter.drawEllipse(earth, radius, radius);
        QColor rim = accent;
        rim.setAlphaF(dark ? 0.45F : 0.35F);
        painter.setPen(QPen(rim, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(earth, radius, radius);
        painter.restore();
        QColor border = text;
        border.setAlphaF(0.10F);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    } else {
        // A quiet band, slightly set off from the window.
        painter.fillRect(rect(), dark ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 8));
    }

    const QStringList label = labels();
    const QFont base = font();
    QFont small = format::scaled(base, 0.85);
    QFont bold = base;
    bold.setBold(true);
    // The satellite's name sits above its disc and the other names below theirs; the padding
    // keeps all of them clear of the edges.
    const double left = 80.0;
    const double right = w - 80.0;
    const std::array<Node, 4> nodes{{
        {Glyph::Server, label[0], {left, ground}},
        // The static diagram leaves more room between the relay and the viewer for their notes.
        {Glyph::Satellite, label[2], {left + (right - left) * (card ? 0.32 : 0.34), sky}},
        {Glyph::Relay, label[4], {left + (right - left) * (card ? 0.64 : 0.70), ground}},
        {Glyph::Device, label[6], {right, ground}},
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
    QColor drift = accent;  // the static diagram's slowly flowing dashes
    drift.setAlphaF(0.75F);
    for (std::size_t i = 0; i < links.size(); ++i) {
        const QPainterPath& path = i == 2 ? access : links[i].path;
        QPen pen(active ? accent : (card ? drift : idle), active ? 2.6 : (card ? 2.0 : 1.6));
        pen.setCapStyle(Qt::RoundCap);
        if (active || card) {
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
        QColor chip = pal.color(QPalette::Window);
        if (card)
            chip.setAlphaF(0.85F);
        painter.setBrush(chip);
        painter.drawRoundedRect(box, 5, 5);
        painter.setPen(active ? accent : secondary);
        painter.drawText(box, Qt::AlignCenter, links[i].label);
    }

    // Nodes: a disc with the glyph, the name underneath (above it for the satellite).
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        const bool viewer = i == 3;
        QColor fill = viewer && stalled_ ? red : accent;
        fill.setAlphaF(dark ? 0.22F : 0.14F);
        QColor ring = viewer && stalled_ ? red : (run_ ? accent : idle);
        painter.setPen(QPen(ring, 1.6));
        painter.setBrush(fill);
        painter.drawEllipse(node.centre, kNodeRadius, kNodeRadius);
        const QIcon icon = glyphIcon(node.glyph, viewer && stalled_ ? red : text);
        const QRectF iconRect(node.centre.x() - 12, node.centre.y() - 12, 24, 24);
        icon.paint(&painter, iconRect.toRect());

        painter.setFont(bold);
        painter.setPen(viewer && stalled_ ? red : text);
        const double labelTop = i == 1 ? node.centre.y() - kNodeRadius - 4.0 - kLabelHeight
                                             - (card ? 18.0 : 0.0)
                                       : node.centre.y() + kNodeRadius + 4.0;
        const QRectF labelRect(node.centre.x() - 150.0, labelTop, 300.0, kLabelHeight);
        painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         viewer && stalled_ ? tr("Viewer · stalled") : node.title);
    }

    if (card) {
        // The heading, and what each node does in a note under its name (beside the satellite,
        // whose name is above it).
        QFont heading = format::scaled(base, 0.8);
        heading.setBold(true);
        heading.setLetterSpacing(QFont::PercentageSpacing, 108);
        heading.setCapitalization(QFont::AllUppercase);
        painter.setFont(heading);
        painter.setPen(secondary);
        painter.drawText(QRectF(kPadding + 6.0, kPadding, w - 2.0 * kPadding, kHeaderHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, tr("How every run was streamed"));
        painter.setFont(small);
        const std::array<QString, 4> notes{tr("Sends the video in segments"),
                                           tr("A 5G base station in orbit"),
                                           tr("Backhauled by satellite"),
                                           tr("An LLM picks the quality")};
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const Node& node = nodes[i];
            QRectF noteRect;
            const Qt::Alignment align = Qt::AlignHCenter;
            if (i == 1) {
                noteRect = QRectF(node.centre.x() - 110.0, node.centre.y() - kNodeRadius - 4.0 - 18.0,
                                  220.0, 18.0);
            } else {
                noteRect = QRectF(node.centre.x() - 110.0, node.centre.y() + kNodeRadius + 4.0 + kLabelHeight,
                                  220.0, 18.0);
            }
            if (i == 3)  // keep the last note inside the card
                noteRect.moveRight(std::min(noteRect.right(), w - kPadding));
            if (i == 0)
                noteRect.moveLeft(std::max(noteRect.left(), kPadding));
            painter.drawText(noteRect, static_cast<int>((align | Qt::AlignVCenter).toInt()), notes[i]);
        }
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
