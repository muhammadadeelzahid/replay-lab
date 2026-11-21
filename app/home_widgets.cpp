#include "home_widgets.h"

#include "branding.h"
#include "format.h"
#include "algorithm_colors.h"
#include "icons.h"

#include <QHBoxLayout>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace abrviz {

namespace {

const QColor kInk(0x14, 0x14, 0x16);  ///< The banner's dark background, a neutral near-black.

/**
 * @brief Draws an image so that it covers a rectangle, cropping what does not fit.
 *
 * @param painter The painter.
 * @param target The rectangle to cover.
 * @param image The image.
 */
void drawCover(QPainter& painter, const QRectF& target, const QImage& image)
{
    const double scale = std::max(target.width() / image.width(), target.height() / image.height());
    const QSizeF size(image.width() * scale, image.height() * scale);
    const QRectF source((size.width() - target.width()) / 2.0 / scale,
                        (size.height() - target.height()) / 2.0 / scale,
                        target.width() / scale, target.height() / scale);
    painter.drawImage(target, image, source);
}

/**
 * @brief The feeder link rate, e.g. "36 Mbps".
 *
 * @param bundle The run.
 * @return The rate, or an empty string if the link was uncongested.
 */
QString feederRate(const BundleSummary& bundle)
{
    return bundle.linkRate ? format::bitrate(static_cast<double>(*bundle.linkRate)) : QString();
}

/**
 * @brief A count with its noun, singular or plural.
 *
 * @param n The count.
 * @param one The noun for one, e.g. "stall".
 * @param many The noun for other counts, e.g. "stalls".
 * @return For example "1 stall" or "59 segments".
 */
QString counted(int n, const QString& one, const QString& many)
{
    return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
}

/**
 * @brief How playback went, in a few words.
 *
 * @param bundle The run.
 * @return "1 stall", "cut off" or "no stalls".
 */
QString outcome(const BundleSummary& bundle)
{
    if (bundle.stallEpisodes > 0)
        return counted(bundle.stallEpisodes, QObject::tr("stall"), QObject::tr("stalls"));
    return bundle.cutOff ? QObject::tr("cut off") : QObject::tr("no stalls");
}

/**
 * @brief Styles a label for the banner: white text at some opacity.
 *
 * @param label The label.
 * @param scale The font size relative to the default.
 * @param bold Whether the text is bold.
 * @param alpha The text's opacity, 0 to 255.
 */
void bannerText(QLabel* label, double scale, bool bold, int alpha)
{
    QFont font = format::scaled(label->font(), scale);
    font.setBold(bold);
    label->setFont(font);
    label->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, %1); background: transparent;").arg(alpha));
}

} // namespace

// ---------------------------------------------------------------------------------------------

QPushButton* pillButton(const QString& text, Glyph glyph, bool primary, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setIcon(glyphIcon(glyph, Qt::white));
    button->setIconSize({18, 18});
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    const QColor accent = accentColor(parent->palette());
    button->setStyleSheet(primary
        ? QStringLiteral("QPushButton { color: white; background: %1; border: none; border-radius: 22px;"
                         " padding: 0 24px; font-weight: 600; min-height: 44px; }"
                         "QPushButton:hover { background: %2; } QPushButton:pressed { background: %3; }")
              .arg(accent.name(), accent.lighter(115).name(), accent.darker(115).name())
        : QStringLiteral("QPushButton { color: white; background: rgba(255,255,255,40); border: none;"
                         " border-radius: 22px; padding: 0 22px; font-weight: 600; min-height: 44px; }"
                         "QPushButton:hover { background: rgba(255,255,255,64); }"
                         "QPushButton:pressed { background: rgba(255,255,255,28); }"));
    return button;
}

SourceCard::SourceCard(Glyph glyph, const QString& title, const QString& detail, bool primary, QWidget* parent)
    : QPushButton(parent)
    , glyph_(glyph)
    , title_(title)
    , detail_(detail)
    , primary_(primary)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(title);
    setAccessibleDescription(detail);
    setMinimumSize(220, 190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize SourceCard::sizeHint() const
{
    return {300, 210};
}

void SourceCard::enterEvent(QEnterEvent* event)
{
    hovered_ = true;
    update();
    QPushButton::enterEvent(event);
}

void SourceCard::leaveEvent(QEvent* event)
{
    hovered_ = false;
    update();
    QPushButton::leaveEvent(event);
}

void SourceCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = accentColor(palette());
    const QRectF frame = QRectF(rect()).adjusted(6, 6, -6, -6);
    // The main card glows: a few soft outlines of the accent around it.
    if (primary_) {
        for (int i = 4; i >= 1; --i) {
            QColor glow = accent;
            glow.setAlphaF(0.05F * static_cast<float>(5 - i));
            painter.setPen(QPen(glow, 1.5 * i));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(frame.adjusted(-i, -i, i, i), 16 + i, 16 + i);
        }
    }
    // Tints of the text colour, so the card works in light and dark mode.
    const QColor ink = palette().color(QPalette::WindowText);
    const auto tint = [&ink](int alpha) { return QColor(ink.red(), ink.green(), ink.blue(), alpha); };
    QLinearGradient fill(frame.topLeft(), frame.bottomLeft());
    const int lift = isDown() ? 6 : (hovered_ ? 20 : 12);
    fill.setColorAt(0, tint(lift + 4));
    fill.setColorAt(1, tint(lift));
    QColor border = primary_ ? accent : tint(hovered_ ? 60 : 28);
    painter.setPen(QPen(border, primary_ ? 1.6 : 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(frame, 16, 16);

    // The icon in a glass tile, the title and the description, centred; a chevron on the right.
    const QRectF tile(frame.center().x() - 30, frame.top() + 26, 60, 60);
    painter.setPen(QPen(tint(32), 1.0));
    painter.setBrush(tint(14));
    painter.drawRoundedRect(tile, 16, 16);
    glyphIcon(glyph_, accent).paint(&painter, tile.adjusted(15, 15, -15, -15).toRect());

    QFont titleFont = format::scaled(font(), 1.4);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(palette().color(QPalette::WindowText));
    const QRectF titleRect(frame.left() + 20, tile.bottom() + 18, frame.width() - 40, 28);
    painter.drawText(titleRect, Qt::AlignCenter, title_);
    painter.setFont(format::scaled(font(), 1.05));
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(QRectF(frame.left() + 24, titleRect.bottom() + 4, frame.width() - 48, frame.bottom() - titleRect.bottom() - 12),
                     Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, detail_);
    glyphIcon(Glyph::Chevron, palette().color(QPalette::PlaceholderText))
        .paint(&painter, QRectF(frame.right() - 30, frame.center().y() - 9, 18, 18).toRect());
}

GlowBackground::GlowBackground(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("glowBackground"));
}

void GlowBackground::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRectF all(rect());
    painter.fillRect(all, palette().color(QPalette::Window));
    // A faint glow of the accent in the top right corner.
    QColor glow = accentColor(palette());
    QRadialGradient light(all.width() * 0.85, -all.height() * 0.1, std::max(all.width(), all.height()) * 0.7);
    glow.setAlphaF(0.08F);
    light.setColorAt(0, glow);
    glow.setAlphaF(0.0F);
    light.setColorAt(1, glow);
    painter.fillRect(all, light);
}

// ---------------------------------------------------------------------------------------------

PathPanel::PathPanel(QWidget* parent)
    : QWidget(parent)
    , animation_(new QTimer(this))
    , heading_(tr("How this run was streamed"))
{
    setObjectName(QStringLiteral("pathPanel"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    animation_->setInterval(33);
    connect(animation_, &QTimer::timeout, this, [this] {
        phase_ += 0.6;
        update();
    });
}

void PathPanel::setDetails(const QString& feeder, const QString& satellite, const QString& backhaul)
{
    feeder_ = feeder;
    satellite_ = satellite;
    backhaul_ = backhaul;
    update();
}

QSize PathPanel::sizeHint() const
{
    return {520, heading_.isEmpty() ? 104 : 124};
}

void PathPanel::setHeading(const QString& heading)
{
    heading_ = heading;
    updateGeometry();
    update();
}

QStringList PathPanel::texts() const
{
    // The same terms as the network path on the Replay page.
    return {heading_, tr("Content server"), tr("LEO gNB"), tr("IAB node"),
            tr("UE"), feeder_.isEmpty() ? tr("Feeder") : feeder_,
            backhaul_.isEmpty() ? tr("Backhaul") : backhaul_, tr("Access")};
}

void PathPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    animation_->start();
}

void PathPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    animation_->stop();
}

void PathPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    painter.setBrush(QColor(8, 12, 24, 165));
    painter.drawRoundedRect(frame, 14, 14);

    const QStringList text = texts();
    QFont heading = format::scaled(font(), 0.75);
    heading.setBold(true);
    heading.setCapitalization(QFont::AllUppercase);
    heading.setLetterSpacing(QFont::PercentageSpacing, 108);
    painter.setFont(heading);
    painter.setPen(QColor(255, 255, 255, 150));
    painter.drawText(QRectF(16, 10, width() - 32, 16), Qt::AlignLeft | Qt::AlignVCenter, text[0]);

    // Four nodes across; the satellite a little higher than the others.
    const double y = heading_.isEmpty() ? height() * 0.5 : height() * 0.55;
    const double left = 48.0;
    const double step = (width() - 2.0 * left) / 3.0;
    const std::array<QPointF, 4> centre{QPointF(left, y + 4), QPointF(left + step, y - 8),
                                        QPointF(left + 2 * step, y + 4), QPointF(left + 3 * step, y + 4)};
    const std::array<Glyph, 4> glyphs{Glyph::Server, Glyph::Satellite, Glyph::Relay, Glyph::Laptop};
    constexpr double radius = 17.0;

    QFont small = format::scaled(font(), 0.8);
    for (int i = 0; i < 3; ++i) {
        const QPointF from = centre[static_cast<std::size_t>(i)] + QPointF(radius + 4, 0);
        const QPointF to = centre[static_cast<std::size_t>(i + 1)] - QPointF(radius + 4, 0);
        QPen pen(QColor(0x64, 0xd2, 0xff), 2.0, Qt::SolidLine, Qt::RoundCap);
        pen.setDashPattern({3.0, 3.0});
        pen.setDashOffset(-phase_);
        painter.setPen(pen);
        painter.drawLine(from, to);
        painter.setFont(small);
        painter.setPen(QColor(255, 255, 255, 190));
        const QRectF label(QPointF(from.x(), std::min(from.y(), to.y()) - 22), QPointF(to.x(), std::min(from.y(), to.y()) - 6));
        painter.drawText(label, Qt::AlignCenter, QFontMetricsF(small).elidedText(text[5 + i], Qt::ElideRight, label.width()));
    }
    QFont name = format::scaled(font(), 0.85);
    name.setBold(true);
    for (std::size_t i = 0; i < centre.size(); ++i) {
        nodeIcon(glyphs[i]).paint(&painter, QRectF(centre[i].x() - 16, centre[i].y() - 16, 32, 32).toRect());
        painter.setFont(name);
        painter.setPen(Qt::white);
        const QRectF below(centre[i].x() - 80, centre[i].y() + radius + 3, 160, 16);
        painter.drawText(below, Qt::AlignCenter, text[static_cast<qsizetype>(i) + 1]);
        if (i == 1 && !satellite_.isEmpty()) {
            painter.setFont(small);
            painter.setPen(QColor(255, 255, 255, 150));
            painter.drawText(below.translated(0, 15), Qt::AlignCenter, satellite_);
        }
    }
}

// ---------------------------------------------------------------------------------------------

RunCard::RunCard(BundleSummaryPtr bundle, QWidget* parent)
    : QAbstractButton(parent)
    , bundle_(std::move(bundle))
{
    setObjectName(QStringLiteral("runCard"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(sizeHint());
    const QString rate = feederRate(*bundle_);
    setToolTip(tr("%1 · %2 feeder · %3\nClick to feature it, double-click to play it, right-click to compare it.")
                   .arg(bundle_->videoTitle.isEmpty() ? bundle_->videoName : bundle_->videoTitle,
                        rate.isEmpty() ? tr("uncongested") : rate, outcome(*bundle_)));
}

void RunCard::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void RunCard::setCaption(const QString& title, const QString& subtitle)
{
    title_ = title;
    subtitle_ = subtitle;
    update();
}

void RunCard::setFeatured(bool featured)
{
    featured_ = featured;
    update();
}

QSize RunCard::sizeHint() const
{
    return {256, 144};
}

void RunCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit opened();
}

void RunCard::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.addAction(tr("Play"), this, &RunCard::opened);
    menu.addAction(tr("Compare Side by Side"), this, &RunCard::compareRequested);
    menu.exec(event->globalPos());
}

void RunCard::enterEvent(QEnterEvent* event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void RunCard::leaveEvent(QEvent* event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void RunCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF frame = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    QPainterPath shape;
    shape.addRoundedRect(frame, 10, 10);
    painter.save();
    painter.setClipPath(shape);
    if (image_.isNull()) {
        QLinearGradient plain(frame.topLeft(), frame.bottomRight());
        plain.setColorAt(0, QColor(0x22, 0x3a, 0x6e));
        plain.setColorAt(1, QColor(0x0d, 0x12, 0x22));
        painter.fillRect(frame, plain);
    } else {
        drawCover(painter, frame, image_);
    }
    QLinearGradient shade(0, frame.top() + frame.height() * 0.35, 0, frame.bottom());
    shade.setColorAt(0, QColor(0, 0, 0, 0));
    shade.setColorAt(1, QColor(0, 0, 0, 200));
    painter.fillRect(frame, shade);
    painter.restore();

    // The feeder rate, large, and the outcome under it.
    QFont title = format::scaled(font(), 1.15);
    title.setBold(true);
    painter.setFont(title);
    painter.setPen(Qt::white);
    const QString rate = feederRate(*bundle_);
    // The algorithm, large, when the run names one; otherwise the feeder rate.
    const QString feeder = rate.isEmpty() ? tr("Uncongested") : tr("%1 feeder").arg(rate);
    const bool named = !bundle_->algorithm.isEmpty();
    const QString heading = !title_.isEmpty() ? title_ : named ? algorithmDisplayName(bundle_->algorithm) : feeder;
    painter.drawText(QRectF(frame.left() + 12, frame.bottom() - 44, frame.width() - 24, 22),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(title).elidedText(heading, Qt::ElideRight, frame.width() - 24));
    QFont small = format::scaled(font(), 0.85);
    painter.setFont(small);
    painter.setPen(QColor(255, 255, 255, 190));
    painter.drawText(QRectF(frame.left() + 12, frame.bottom() - 24, frame.width() - 24, 16),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     !subtitle_.isEmpty() ? subtitle_
                     : named ? tr("%1 · %2").arg(feeder, counted(bundle_->segments, tr("segment"), tr("segments")))
                             : counted(bundle_->segments, tr("segment"), tr("segments")));

    // A badge for how playback went: red for stalls, orange for a cut-off, green otherwise.
    const QString badge = outcome(*bundle_);
    const QColor badgeColor = bundle_->stallEpisodes > 0 ? QColor(0xff, 0x45, 0x3a)
                              : bundle_->cutOff   ? QColor(0xff, 0x9f, 0x0a)
                                                  : QColor(0x30, 0xd1, 0x58);
    const double badgeWidth = QFontMetricsF(small).horizontalAdvance(badge) + 16;
    const QRectF badgeRect(frame.left() + 10, frame.top() + 10, badgeWidth, 20);
    painter.setPen(Qt::NoPen);
    painter.setBrush(badgeColor);
    painter.drawRoundedRect(badgeRect, 10, 10);
    painter.setPen(bundle_->stallEpisodes > 0 || bundle_->cutOff ? Qt::white : QColor(0x0b, 0x2a, 0x12));
    painter.drawText(badgeRect, Qt::AlignCenter, badge);

    if (featured_ || hovered_) {
        painter.setPen(QPen(featured_ ? accentColor(palette()) : QColor(255, 255, 255, 200), featured_ ? 3.0 : 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
    }
}

// ---------------------------------------------------------------------------------------------

FeaturedBanner::FeaturedBanner(QWidget* parent)
    : QWidget(parent)
    , title_(new QLabel(this))
    , meta_(new QLabel(this))
    , highlight_(new QLabel(this))
    , description_(new QLabel(this))
    , compare_(nullptr)
    , chips_(new QLabel(this))
    , play_(new QPushButton(tr("Play"), this))
    , info_(new QPushButton(tr("More Info"), this))
    , path_(new PathPanel(this))
{
    setObjectName(QStringLiteral("featuredBanner"));
    setFixedHeight(460);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* brand = new QWidget(this);
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(8);
    auto* icon = new QLabel(brand);
    icon->setPixmap(appIcon().pixmap(26, 26));
    auto* brandText = new QLabel(tr("<b>%1</b> run").arg(appDisplayName().toHtmlEscaped()), brand);
    bannerText(brandText, 1.1, false, 235);
    brandLayout->addWidget(icon);
    brandLayout->addWidget(brandText);
    brandLayout->addStretch(1);

    bannerText(title_, 3.0, true, 255);
    title_->setWordWrap(true);
    bannerText(meta_, 1.0, false, 220);
    meta_->setTextFormat(Qt::RichText);
    policy_ = new QLabel(this);
    policy_->setStyleSheet(QStringLiteral("color: white; background: transparent; border: 1px solid rgba(255,255,255,170);"
                                         " border-radius: 5px; padding: 1px 7px;"));
    auto* metaRow = new QHBoxLayout();
    metaRow->setSpacing(14);
    metaRow->addWidget(meta_);
    metaRow->addWidget(policy_);
    metaRow->addStretch(1);
    bannerText(highlight_, 1.25, true, 255);
    bannerText(description_, 1.0, false, 205);
    description_->setWordWrap(true);

    const QColor accent = accentColor(palette());
    play_->setIcon(glyphIcon(Glyph::Play, Qt::white));
    play_->setIconSize({18, 18});
    play_->setCursor(Qt::PointingHandCursor);
    play_->setStyleSheet(QStringLiteral(
        "QPushButton { color: white; background: %1; border: none; border-radius: 22px; padding: 0 26px;"
        " font-weight: 600; min-height: 44px; }"
        "QPushButton:hover { background: %2; } QPushButton:pressed { background: %3; }")
                             .arg(accent.name(), accent.lighter(115).name(), accent.darker(115).name()));
    info_->setIcon(glyphIcon(Glyph::Report, Qt::white));
    info_->setIconSize({18, 18});
    info_->setCursor(Qt::PointingHandCursor);
    info_->setStyleSheet(QStringLiteral(
        "QPushButton { color: white; background: rgba(255,255,255,46); border: none; border-radius: 22px;"
        " padding: 0 22px; font-weight: 600; min-height: 44px; }"
        "QPushButton:hover { background: rgba(255,255,255,70); } QPushButton:pressed { background: rgba(255,255,255,30); }"));
    connect(play_, &QPushButton::clicked, this, &FeaturedBanner::playRequested);
    connect(info_, &QPushButton::clicked, this, &FeaturedBanner::infoRequested);
    // Compare comes first when the run has other algorithms; Play is then the glass button.
    compare_ = pillButton(tr("Compare"), Glyph::Analysis, true, this);
    connect(compare_, &QPushButton::clicked, this, &FeaturedBanner::compareRequested);
    compare_->hide();
    bannerText(chips_, 0.95, false, 235);
    chips_->setTextFormat(Qt::RichText);
    chips_->hide();

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(12);
    buttons->addWidget(compare_);
    buttons->addWidget(play_);
    buttons->addWidget(info_);
    buttons->addStretch(1);

    auto* column = new QVBoxLayout();
    column->setSpacing(0);
    column->addStretch(1);
    column->addWidget(brand);
    column->addSpacing(10);
    column->addWidget(title_);
    column->addSpacing(8);
    column->addLayout(metaRow);
    column->addSpacing(18);
    column->addWidget(highlight_);
    column->addSpacing(6);
    column->addWidget(chips_);
    column->addSpacing(12);
    column->addWidget(description_);
    column->addSpacing(24);
    column->addLayout(buttons);
    column->addStretch(1);
    auto* text = new QWidget(this);
    text->setLayout(column);
    text->setMaximumWidth(540);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(44, 20, 44, 24);
    layout->addWidget(text, 1);
    layout->addStretch(1);
    setBundle(nullptr, {}, {});
}

void FeaturedBanner::setBundle(BundleSummaryPtr bundle, const QString& satellite, const QString& backhaul)
{
    bundle_ = std::move(bundle);
    image_ = QImage();
    const bool has = bundle_ != nullptr;
    for (QWidget* widget : std::initializer_list<QWidget*>{play_, info_})
        widget->setVisible(has);
    path_->setVisible(has && width() >= 560 + path_->sizeHint().width());
    if (!has) {
        title_->clear();
        meta_->clear();
        highlight_->clear();
        description_->clear();
        update();
        return;
    }
    const BundleSummary& run = *bundle_;
    title_->setText(run.videoTitle.isEmpty() ? run.videoName : run.videoTitle);
    const QString rate = feederRate(run);
    meta_->setText(QStringLiteral("%1 &nbsp;&nbsp;&nbsp; %2")
                       .arg(rate.isEmpty() ? tr("Uncongested feeder") : tr("%1 feeder").arg(rate),
                            counted(run.segments, tr("segment"), tr("segments"))));
    const QString algorithm = algorithmDisplayName(run.algorithm);
    policy_->setText(algorithm);
    if (run.stallEpisodes > 0)
        highlight_->setText(run.stallEpisodes == 1
                                ? tr("Stalled once over the satellite, for %1 s").arg(run.stalledTime, 0, 'f', 1)
                                : tr("Stalled %1 times over the satellite, %2 s in all")
                                      .arg(run.stallEpisodes)
                                      .arg(run.stalledTime, 0, 'f', 1));
    else if (run.cutOff)
        highlight_->setText(tr("The simulation stopped during playback"));
    else
        highlight_->setText(tr("Played without a stall"));
    description_->setText(tr("Streamed to one of three UEs over a LEO satellite 5G network, through an IAB "
                             "node. %1 chose the quality of every segment as the buffer rose and fell.")
                              .arg(run.algorithm.compare(QLatin1String("llm"), Qt::CaseInsensitive) == 0
                                       ? tr("An LLM") : algorithm));
    path_->setDetails(rate.isEmpty() ? QString() : tr("Feeder · %1").arg(rate), satellite, backhaul);
    update();
}

void FeaturedBanner::setHighlightText(const QString& text)
{
    highlight_->setText(text);
}

void FeaturedBanner::setExperiment(const QList<BundleSummaryPtr>& runs)
{
    const bool compare = runs.size() >= 2 && bundle_;
    compare_->setVisible(compare);
    chips_->setVisible(compare);
    // Play becomes the glass button next to Compare, the main one otherwise.
    const QColor accent = accentColor(palette());
    play_->setStyleSheet(compare
        ? QStringLiteral("QPushButton { color: white; background: rgba(255,255,255,46); border: none; border-radius: 22px;"
                         " padding: 0 22px; font-weight: 600; min-height: 44px; }"
                         "QPushButton:hover { background: rgba(255,255,255,70); } QPushButton:pressed { background: rgba(255,255,255,30); }")
        : QStringLiteral("QPushButton { color: white; background: %1; border: none; border-radius: 22px; padding: 0 26px;"
                         " font-weight: 600; min-height: 44px; }"
                         "QPushButton:hover { background: %2; } QPushButton:pressed { background: %3; }")
              .arg(accent.name(), accent.lighter(115).name(), accent.darker(115).name()));
    if (!compare)
        return;
    QStringList chips;
    for (const BundleSummaryPtr& run : runs) {
        const QColor color = algorithmColor(run->algorithm, palette());
        const QString name = algorithmDisplayName(run->algorithm);
        const QString state = outcome(*run) + (run->stallEpisodes > 0 ? tr(", %1 s").arg(run->stalledTime, 0, 'f', 1) : QString());
        const bool featured = bundle_ && run->directory == bundle_->directory;
        chips << QStringLiteral("<span style='color:%1'>&#9679;</span>&nbsp;%2%3&nbsp;<span style='color:rgba(255,255,255,160)'>%4</span>")
                     .arg(color.name(), featured ? QStringLiteral("<b>") : QString(),
                          name + (featured ? QStringLiteral("</b>") : QString()), state.toHtmlEscaped());
    }
    chips_->setText(chips.join(QStringLiteral(" &nbsp;&nbsp;&nbsp; ")));
}

void FeaturedBanner::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void FeaturedBanner::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const QSize size = path_->sizeHint();
    path_->setGeometry(width() - 44 - size.width(), height() - 28 - size.height(), size.width(), size.height());
    // Beside the text only when there is room for both.
    path_->setVisible(bundle_ && width() >= 560 + size.width());
}

void FeaturedBanner::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF all(rect());
    // A dark card with rounded corners, in light and dark mode alike.
    QPainterPath card;
    card.addRoundedRect(all, 16, 16);
    painter.setClipPath(card);
    painter.fillRect(all, kInk);
    if (!image_.isNull()) {
        // The still on the right two thirds, fading into the dark on the left.
        const QRectF picture(all.width() * 0.30, 0, all.width() * 0.70, all.height());
        drawCover(painter, picture, image_);
        QLinearGradient fade(picture.left(), 0, picture.left() + picture.width() * 0.45, 0);
        fade.setColorAt(0, kInk);
        fade.setColorAt(1, QColor(kInk.red(), kInk.green(), kInk.blue(), 0));
        painter.fillRect(picture, fade);
    } else {
        QRadialGradient glow(all.width() * 0.75, all.height() * 0.3, all.width() * 0.6);
        glow.setColorAt(0, QColor(0x1d, 0x3f, 0x86));
        glow.setColorAt(1, kInk);
        painter.fillRect(all, glow);
    }
    // A soft shade along the top and the bottom.
    QLinearGradient top(0, 0, 0, 70);
    top.setColorAt(0, QColor(kInk.red(), kInk.green(), kInk.blue(), 170));
    top.setColorAt(1, QColor(kInk.red(), kInk.green(), kInk.blue(), 0));
    painter.fillRect(QRectF(0, 0, all.width(), 70), top);
    QLinearGradient bottom(0, all.height() - 110, 0, all.height());
    bottom.setColorAt(0, QColor(kInk.red(), kInk.green(), kInk.blue(), 0));
    bottom.setColorAt(1, kInk);
    painter.fillRect(QRectF(0, all.height() - 110, all.width(), 110), bottom);
}

} // namespace abrviz
