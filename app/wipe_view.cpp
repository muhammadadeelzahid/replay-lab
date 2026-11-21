#include "wipe_view.h"

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsVideoItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRegion>

#include <algorithm>
#include <cmath>

namespace abrviz {

namespace {

constexpr int kLoupeSize = 220;     ///< Diameter of the loupe, in pixels.
constexpr double kLoupeZoom = 2.0;  ///< Magnification of the loupe.
constexpr double kGrab = 8.0;       ///< How close to the divider a press grabs it, in pixels.

} // namespace

WipeView::WipeView(QWidget* parent)
    : QGraphicsView(parent)
    , scene_(new QGraphicsScene(this))
    , left_(new QGraphicsVideoItem())
    , clip_(new QGraphicsRectItem())
    , right_(new QGraphicsVideoItem())
    , line_(new QGraphicsRectItem())
    , leftTag_(new QGraphicsRectItem())
    , leftText_(new QGraphicsSimpleTextItem())
    , rightTag_(new QGraphicsRectItem())
    , rightText_(new QGraphicsSimpleTextItem())
    , loupe_(new QGraphicsView(this))
{
    setObjectName(QStringLiteral("wipeView"));
    setScene(scene_);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(Qt::black);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);

    // The scene owns its items: the left video, the clip holding the right video, the
    // divider and the two labels.
    left_->setAspectRatioMode(Qt::KeepAspectRatio);
    scene_->addItem(left_);
    clip_->setPen(Qt::NoPen);
    clip_->setBrush(Qt::NoBrush);
    clip_->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
    scene_->addItem(clip_);
    right_->setAspectRatioMode(Qt::KeepAspectRatio);
    right_->setParentItem(clip_);
    line_->setPen(Qt::NoPen);
    line_->setBrush(QColor(255, 255, 255, 230));
    line_->setZValue(2);
    scene_->addItem(line_);
    for (auto [tag, text] : {std::pair{leftTag_, leftText_}, std::pair{rightTag_, rightText_}}) {
        tag->setPen(Qt::NoPen);
        tag->setBrush(QColor(0, 0, 0, 170));
        tag->setZValue(3);
        scene_->addItem(tag);
        QFont font = text->font();
        font.setPixelSize(13);
        font.setBold(true);
        text->setFont(font);
        text->setParentItem(tag);
        text->setPos(8, 4);
    }

    // The loupe: a round view of the same scene, magnified, that follows the pointer.
    loupe_->setScene(scene_);
    loupe_->setFixedSize(kLoupeSize, kLoupeSize);
    loupe_->setFrameShape(QFrame::NoFrame);
    loupe_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    loupe_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    loupe_->setInteractive(false);
    loupe_->setAttribute(Qt::WA_TransparentForMouseEvents);
    loupe_->setBackgroundBrush(Qt::black);
    loupe_->setRenderHint(QPainter::SmoothPixmapTransform);
    loupe_->scale(kLoupeZoom, kLoupeZoom);
    loupe_->setMask(QRegion(0, 0, kLoupeSize, kLoupeSize, QRegion::Ellipse));
    loupe_->hide();
    layoutScene();
}

QVideoSink* WipeView::leftSink() const
{
    return left_->videoSink();
}

QVideoSink* WipeView::rightSink() const
{
    return right_->videoSink();
}

void WipeView::setLabels(const QString& left, const QString& right, const QColor& leftColor,
                         const QColor& rightColor)
{
    leftText_->setText(left);
    leftText_->setBrush(leftColor);
    rightText_->setText(right);
    rightText_->setBrush(rightColor);
    layoutScene();
}

void WipeView::setDivider(double fraction)
{
    divider_ = std::isfinite(fraction) ? std::clamp(fraction, 0.0, 1.0) : 0.5;
    layoutScene();
}

bool WipeView::isLoupeVisible() const
{
    return loupe_->isVisible();
}

void WipeView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    layoutScene();
}

void WipeView::layoutScene()
{
    const QSizeF size = viewport()->size();
    const QRectF all(QPointF(0, 0), size);
    scene_->setSceneRect(all);
    left_->setSize(size);
    right_->setSize(size);
    const double x = std::round(size.width() * divider_);
    clip_->setRect(QRectF(x, 0, size.width() - x, size.height()));
    line_->setRect(QRectF(x - 1.0, 0, 2.0, size.height()));
    for (auto [tag, text] : {std::pair{leftTag_, leftText_}, std::pair{rightTag_, rightText_}})
        tag->setRect(QRectF(0, 0, text->boundingRect().width() + 16, text->boundingRect().height() + 8));
    leftTag_->setPos(12, 12);
    rightTag_->setPos(size.width() - rightTag_->rect().width() - 12, 12);
    leftTag_->setVisible(!leftText_->text().isEmpty());
    rightTag_->setVisible(!rightText_->text().isEmpty());
}

void WipeView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        setDivider(event->position().x() / std::max(1, viewport()->width()));
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void WipeView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    if (dragging_)
        setDivider(at.x() / std::max(1, viewport()->width()));
    // Near the divider the cursor shows it can be dragged.
    const double x = viewport()->width() * divider_;
    viewport()->setCursor(dragging_ || std::abs(at.x() - x) < kGrab ? Qt::SplitHCursor : Qt::CrossCursor);
    // The loupe, centred on the pointer, shows the scene around it magnified.
    if (!dragging_) {
        loupe_->move(QPoint(static_cast<int>(at.x()) - kLoupeSize / 2, static_cast<int>(at.y()) - kLoupeSize / 2));
        loupe_->centerOn(mapToScene(at.toPoint()));
        loupe_->show();
        loupe_->raise();
    } else {
        loupe_->hide();
    }
}

void WipeView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void WipeView::leaveEvent(QEvent* event)
{
    loupe_->hide();
    QGraphicsView::leaveEvent(event);
}

void WipeView::keyPressEvent(QKeyEvent* event)
{
    const double step = (event->modifiers() & Qt::ShiftModifier) ? 0.1 : 0.02;
    if (event->key() == Qt::Key_Left) {
        setDivider(divider_ - step);
        return;
    }
    if (event->key() == Qt::Key_Right) {
        setDivider(divider_ + step);
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

} // namespace abrviz
