#include "ui/trim_editor_dialog.hpp"

#include "ui/mpv_process_backend.hpp"
#include "ui/timeline_trim_overlay.hpp"
#include "ui/trim_panel.hpp"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QPalette>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace billfish::trim {

namespace {
constexpr auto kTimelineObjectName = "PreviewControlWidget_m_pVideoSlider";
constexpr auto kPlayButtonObjectName = "PreviewControlWidget_StartBnt";
constexpr auto kPauseButtonObjectName = "PreviewControlWidget_PauseBnt";
}

TrimEditorDialog::TrimEditorDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint) {
    setObjectName(QString::fromLatin1(kObjectName));
    setProperty(kEditorHostProperty, true);
    setWindowTitle(QStringLiteral("剪辑视频"));
    setModal(false);
    setMinimumSize(720, 540);
    resize(960, 825);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    videoHost_ = new QWidget(this);
    videoHost_->setObjectName(QStringLiteral("BillfishVideoTrimMpvHost"));
    videoHost_->setAttribute(Qt::WA_NativeWindow, true);
    videoHost_->setAutoFillBackground(true);
    QPalette videoPalette = videoHost_->palette();
    videoPalette.setColor(QPalette::Window, Qt::black);
    videoHost_->setPalette(videoPalette);
    auto* videoLayout = new QVBoxLayout(videoHost_);
    videoLayout->setContentsMargins(24, 24, 24, 24);
    videoStatusLabel_ = new QLabel(QStringLiteral("正在准备播放器..."), videoHost_);
    videoStatusLabel_->setObjectName(QStringLiteral("BillfishVideoTrimPlaybackStatus"));
    videoStatusLabel_->setAlignment(Qt::AlignCenter);
    videoStatusLabel_->setStyleSheet(QStringLiteral("color:#A2ABC3;background:transparent;"));
    videoLayout->addWidget(videoStatusLabel_, 0, Qt::AlignCenter);
    rootLayout->addWidget(videoHost_, 1);

    timeline_ = new QSlider(Qt::Horizontal, this);
    timeline_->setObjectName(QString::fromLatin1(kTimelineObjectName));
    timeline_->setRange(0, kTimelineResolution);
    timeline_->setValue(0);
    timeline_->setPageStep(kTimelineResolution / 20);
    timeline_->setFixedHeight(40);
    timeline_->setFocusPolicy(Qt::StrongFocus);
    rootLayout->addWidget(timeline_);

    hostPlayButton_ = new QToolButton(this);
    hostPlayButton_->setObjectName(QString::fromLatin1(kPlayButtonObjectName));
    hostPlayButton_->hide();
    hostPauseButton_ = new QToolButton(this);
    hostPauseButton_->setObjectName(QString::fromLatin1(kPauseButtonObjectName));
    hostPauseButton_->hide();

    trimPanel_ = new TrimPanel(this);
    trimPanel_->setObjectName(QString::fromLatin1(TrimPanel::kObjectName));
    rootLayout->addWidget(trimPanel_);
    const bool attached = trimPanel_->attachToPlayer(this);
    Q_ASSERT(attached);

    playbackBackend_ = new MpvProcessBackend(videoHost_, this);
    scrubSeekTimer_ = new QTimer(this);
    scrubSeekTimer_->setSingleShot(true);
    scrubSeekTimer_->setInterval(16);
    connect(scrubSeekTimer_, &QTimer::timeout, this, [this] {
        if (pendingScrubPositionMs_ < 0) {
            return;
        }
        const qint64 positionMs = pendingScrubPositionMs_;
        pendingScrubPositionMs_ = -1;
        requestPlaybackSeek(positionMs);
    });
    connect(playbackBackend_, &MpvProcessBackend::ready, this, [this] {
        videoStatusLabel_->setText(QStringLiteral("正在打开视频..."));
    });
    connect(playbackBackend_, &MpvProcessBackend::fileLoaded, this, [this] {
        videoStatusLabel_->hide();
        const RangeSelection range = trimPanel_->selection();
        if (trimPanel_->isPreviewing() && range.isValid()) {
            // A preview may have been requested while mpv was still loading.
            // Reassert the final playback state after file-loaded so late mpv
            // initialization cannot leave the playhead at zero or drop A/B.
            (void)playbackBackend_->setAbLoop(range.startMs, range.endMs);
            (void)playbackBackend_->seek(range.startMs);
            (void)playbackBackend_->play();
            return;
        }
        (void)playbackBackend_->clearAbLoop();
        (void)playbackBackend_->pause();
        (void)playbackBackend_->seek(initialPositionMs_);
    });
    connect(playbackBackend_, &MpvProcessBackend::positionChanged, this,
            [this](const qint64 positionMs) {
                if (!timeline_->isSliderDown()) {
                    timeline_->setValue(timelineValueForMs(positionMs));
                }
            });
    connect(playbackBackend_, &MpvProcessBackend::durationChanged, this,
            [this](const qint64 durationMs) {
                if (durationMs <= 0 || sourcePath_.isEmpty()) {
                    return;
                }
                mediaDurationMs_ = durationMs;
                setProperty("billfish.trim.durationMs", durationMs);
                trimPanel_->applyVerifiedSource(sourcePath_, durationMs);
                if (!initialPositionApplied_) {
                    applyInitialPosition();
                }
            });
    connect(playbackBackend_, &MpvProcessBackend::pausedChanged, this,
            [this](const bool paused) {
                if (paused && trimPanel_->isPreviewing()) {
                    trimPanel_->stopPreview();
                }
            });
    connect(playbackBackend_, &MpvProcessBackend::errorOccurred,
            this, &TrimEditorDialog::showPlaybackError);

    connect(hostPlayButton_, &QToolButton::clicked, this, [this] {
        (void)playbackBackend_->play();
    });
    connect(hostPauseButton_, &QToolButton::clicked, this, [this] {
        (void)playbackBackend_->pause();
    });
    connect(timeline_, &QSlider::sliderPressed, this, [this] {
        resumeAfterScrub_ = !playbackBackend_->isPaused();
        (void)playbackBackend_->pause();
    });
    connect(timeline_, &QSlider::sliderMoved, this, [this](const int value) {
        pendingScrubPositionMs_ = positionMsForTimelineValue(value);
        if (!scrubSeekTimer_->isActive()) {
            scrubSeekTimer_->start();
        }
    });
    connect(timeline_, &QSlider::sliderReleased, this, [this] {
        scrubSeekTimer_->stop();
        pendingScrubPositionMs_ = -1;
        requestPlaybackSeek(positionMsForTimelineValue(timeline_->value()));
        if (resumeAfterScrub_) {
            (void)playbackBackend_->play();
        }
        resumeAfterScrub_ = false;
    });
    connect(trimPanel_, &TrimPanel::sourceChanged, this,
            [this](const QString& path, const qint64 durationMs) {
                if (durationMs <= 0 ||
                    QString::compare(path, sourcePath_, Qt::CaseInsensitive) != 0) {
                    return;
                }
                mediaDurationMs_ = durationMs;
                setProperty("billfish.trim.durationMs", durationMs);
                if (!initialPositionApplied_) {
                    applyInitialPosition();
                }
            });
    connect(trimPanel_, &TrimPanel::previewStarted, this,
            [this](const qint64 startMs, const qint64 endMs) {
                (void)playbackBackend_->setAbLoop(startMs, endMs);
            });
    connect(trimPanel_, &TrimPanel::previewStopped, this, [this] {
        (void)playbackBackend_->clearAbLoop();
    });
    connect(trimPanel_, &TrimPanel::selectionChanged, this,
            [this](const qint64 startMs, const qint64 endMs) {
                if (trimPanel_->isPreviewing()) {
                    (void)playbackBackend_->setAbLoop(startMs, endMs);
                }
            });
    connect(trimPanel_, &TrimPanel::exportFinished,
            this, &TrimEditorDialog::finishPendingClose);
    connect(trimPanel_, &TrimPanel::copyFinished,
            this, &TrimEditorDialog::finishPendingClose);
    connect(trimPanel_, &TrimPanel::operationFailed,
            this, &TrimEditorDialog::finishPendingClose);

    auto* setInShortcut = new QShortcut(QKeySequence(QStringLiteral("I")), this);
    connect(setInShortcut, &QShortcut::activated, this, [this] {
        if (trimPanel_->timelineOverlay() != nullptr) {
            trimPanel_->timelineOverlay()->setInValue(timeline_->value());
        }
    });
    auto* setOutShortcut = new QShortcut(QKeySequence(QStringLiteral("O")), this);
    connect(setOutShortcut, &QShortcut::activated, this, [this] {
        if (trimPanel_->timelineOverlay() != nullptr) {
            trimPanel_->timelineOverlay()->setOutValue(timeline_->value());
        }
    });
    auto* playbackShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playbackShortcut, &QShortcut::activated, this, [this] {
        if (auto* button = trimPanel_->findChild<QToolButton*>(
                QStringLiteral("BillfishVideoTrimPreviewButton"))) {
            button->click();
        }
    });
}

TrimEditorDialog::~TrimEditorDialog() {
    if (playbackBackend_ != nullptr) {
        trimPanel_->stopPreview();
        playbackBackend_->shutdown();
        delete playbackBackend_;
        playbackBackend_ = nullptr;
    }
}

void TrimEditorDialog::setSource(const QString& sourcePath, const qint64 durationMs,
                                 const qint64 initialPositionMs) {
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile()) {
        showPlaybackError(QStringLiteral("当前视频文件不存在"));
        return;
    }
    const QString absolutePath = sourceInfo.absoluteFilePath();
    if (!sourcePath_.isEmpty()) {
        return;
    }
    sourcePath_ = absolutePath;
    mediaDurationMs_ = durationMs > 0 ? durationMs : -1;
    initialPositionMs_ = std::max<qint64>(0, initialPositionMs);
    initialPositionApplied_ = false;
    setWindowTitle(QStringLiteral("剪辑视频 - %1").arg(sourceInfo.fileName()));
    setProperty("billfish.trim.inputPath", sourcePath_);
    setProperty("billfish.trim.durationMs", mediaDurationMs_);
    timeline_->setValue(0);
    // Bind the immutable source snapshot before playback starts. TrimPanel
    // owns the independent FFprobe fallback, so export remains available even
    // when mpv is missing or cannot initialize.
    (void)trimPanel_->refreshRuntimeBindings(this);
    if (mediaDurationMs_ > 0) {
        trimPanel_->applyVerifiedSource(sourcePath_, mediaDurationMs_);
        if (!initialPositionApplied_) {
            applyInitialPosition();
        }
    }
    videoStatusLabel_->setText(QStringLiteral("正在打开 %1...").arg(sourceInfo.fileName()));
    videoStatusLabel_->show();

    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        videoStatusLabel_->setText(QStringLiteral("离屏测试模式"));
        return;
    }
    if (!playbackBackend_->isRunning() &&
        !playbackBackend_->start(QCoreApplication::applicationDirPath())) {
        showPlaybackError(playbackBackend_->lastError());
        return;
    }
    loadSourceInPlaybackBackend();
}

QString TrimEditorDialog::sourcePath() const { return sourcePath_; }
qint64 TrimEditorDialog::mediaDurationMs() const noexcept { return mediaDurationMs_; }
TrimPanel* TrimEditorDialog::trimPanel() const noexcept { return trimPanel_; }
QSlider* TrimEditorDialog::timeline() const noexcept { return timeline_; }
QWidget* TrimEditorDialog::videoHost() const noexcept { return videoHost_; }
MpvProcessBackend* TrimEditorDialog::playbackBackend() const noexcept {
    return playbackBackend_;
}
bool TrimEditorDialog::isBusy() const noexcept { return trimPanel_->isBusy(); }
QSize TrimEditorDialog::sizeHint() const { return QSize(960, 825); }

void TrimEditorDialog::closeEvent(QCloseEvent* event) {
    if (trimPanel_->isBusy()) {
        pendingClose_ = true;
        trimPanel_->cancelExport();
        event->ignore();
        return;
    }
    trimPanel_->stopPreview();
    playbackBackend_->shutdown();
    QDialog::closeEvent(event);
}

int TrimEditorDialog::timelineValueForMs(const qint64 positionMs) const noexcept {
    if (mediaDurationMs_ <= 0) {
        return 0;
    }
    const double fraction = static_cast<double>(positionMs) /
                            static_cast<double>(mediaDurationMs_);
    return static_cast<int>(std::llround(
        std::clamp(fraction, 0.0, 1.0) * kTimelineResolution));
}

qint64 TrimEditorDialog::positionMsForTimelineValue(const int value) const noexcept {
    if (mediaDurationMs_ <= 0) {
        return 0;
    }
    const double fraction = static_cast<double>(value) /
                            static_cast<double>(kTimelineResolution);
    return static_cast<qint64>(std::llround(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(mediaDurationMs_)));
}

void TrimEditorDialog::applyInitialPosition() {
    if (mediaDurationMs_ <= 0 || initialPositionApplied_) {
        return;
    }
    const qint64 latestValidStart = std::max<qint64>(0, mediaDurationMs_ - 1);
    initialPositionMs_ = std::clamp(initialPositionMs_, qint64{0}, latestValidStart);
    int timelineValue = timelineValueForMs(initialPositionMs_);
    if (timeline_->maximum() > timeline_->minimum()) {
        timelineValue = std::min(timelineValue, timeline_->maximum() - 1);
    }
    timeline_->setValue(timelineValue);
    if (trimPanel_->timelineOverlay() != nullptr) {
        trimPanel_->timelineOverlay()->setInValue(timelineValue);
    }
    initialPositionApplied_ = true;
}

void TrimEditorDialog::requestPlaybackSeek(const qint64 positionMs) {
    emit scrubSeekRequested(positionMs);
    (void)playbackBackend_->seek(positionMs);
}

void TrimEditorDialog::loadSourceInPlaybackBackend() {
    if (!sourcePath_.isEmpty()) {
        (void)playbackBackend_->load(sourcePath_);
    }
}

void TrimEditorDialog::finishPendingClose() {
    if (pendingClose_ && !trimPanel_->isBusy()) {
        pendingClose_ = false;
        close();
    }
}

void TrimEditorDialog::showPlaybackError(const QString& error) {
    const QString message = error.trimmed().isEmpty()
        ? QStringLiteral("无法启动视频播放器")
        : error.trimmed();
    videoStatusLabel_->setText(QStringLiteral("播放不可用\n%1").arg(message));
    videoStatusLabel_->setToolTip(message);
    videoStatusLabel_->show();
}

}  // namespace billfish::trim
