#include "format.h"
#include "settings_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QUrl>
#include <QVBoxLayout>

namespace abrviz {

namespace {

/**
 * @brief A word-wrapped explanatory note in smaller, secondary text, 340 pixels wide.
 *
 * @param text The note.
 * @param parent The label's parent.
 * @return The new label, owned by `parent`.
 */
QLabel* note(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    QFont font = label->font();
    font = format::scaled(font, 0.9);
    label->setFont(font);
    label->setFixedWidth(340);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(const QString& renderCache, QWidget* parent)
    : QDialog(parent)
    , engines_(new QButtonGroup(this))
    , overlay_(new QCheckBox(tr("Show information over the video"), this))
    , cache_(new QLabel(this))
{
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("settingsDialog"));

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);

    auto* engineBox = new QVBoxLayout();
    engineBox->setSpacing(4);
    for (const Engine engine : {Engine::GStreamer, Engine::Prerendered}) {
        auto* button = new QRadioButton(engineName(engine), this);
        button->setEnabled(ReplaySourceFactory::isAvailable(engine));
        engines_->addButton(button, static_cast<int>(engine));
        engineBox->addWidget(button);
    }
    engineBox->addWidget(note(tr("GStreamer plays the segment files directly. The pre-rendered engine "
                                 "plays a video rendered once with ffmpeg and kept in the cache."),
                              this));
    form->addRow(tr("Replay engine:"), engineBox);
    form->addRow(tr("Video:"), overlay_);

    cache_->setText(QDir::toNativeSeparators(renderCache));
    cache_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cache_->setWordWrap(true);
    cache_->setFixedWidth(340);
    auto* reveal = new QPushButton(tr("Show in Finder"), this);
    connect(reveal, &QPushButton::clicked, this, [renderCache] {
        QDir().mkpath(renderCache);
        QDesktopServices::openUrl(QUrl::fromLocalFile(renderCache));
    });
    auto* cacheBox = new QVBoxLayout();
    cacheBox->addWidget(cache_);
    cacheBox->addWidget(reveal, 0, Qt::AlignLeft);
    form->addRow(tr("Rendered videos:"), cacheBox);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->addLayout(form);
    setFixedSize(sizeHint());

    connect(engines_, &QButtonGroup::idClicked, this,
            [this](int id) { emit engineChosen(static_cast<Engine>(id)); });
    connect(overlay_, &QCheckBox::toggled, this, &SettingsDialog::overlayToggled);
}

void SettingsDialog::setEngine(Engine engine)
{
    if (QAbstractButton* button = engines_->button(static_cast<int>(engine)))
        button->setChecked(true);
}

void SettingsDialog::setOverlayVisible(bool visible)
{
    const QSignalBlocker block(overlay_);
    overlay_->setChecked(visible);
}

} // namespace abrviz
