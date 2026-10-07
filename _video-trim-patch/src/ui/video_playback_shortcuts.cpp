#include "ui/video_playback_shortcuts.hpp"
#include "ui/trim_diagnostics.hpp"
#include "ui/trim_editor_dialog.hpp"

#include <QApplication>
#include <QAbstractSlider>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaMethod>
#include <QTimer>
#include <QToolTip>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <limits>

namespace billfish::trim {
namespace {

QWidget* ancestor(QWidget* widget, const char* className) {
    for (auto* current = widget; current; current = current->parentWidget()) {
        if (current->inherits(className)) return current;
    }
    return nullptr;
}

bool within(QWidget* widget, QWidget* parent) {
    for (auto* current = widget; current; current = current->parentWidget()) {
        if (current == parent) return true;
    }
    return false;
}

QWidget* mediaSurface(QWidget* video) {
    if (!video) return nullptr;
    for (auto* child : video->children()) {
        auto* widget = qobject_cast<QWidget*>(child);
        if (widget && widget->inherits("MediaWidget") && widget->isVisible()) return widget;
    }
    return nullptr;
}

bool editing(QWidget* widget) {
    for (auto* current = widget; current; current = current->parentWidget()) {
        if (current->inherits("QLineEdit") || current->inherits("QTextEdit") ||
            current->inherits("QPlainTextEdit") || current->inherits("QComboBox") ||
            current->inherits("QAbstractSpinBox") ||
            current->property(TrimEditorDialog::kEditorHostProperty).toBool() ||
            current->objectName() == QString::fromLatin1(TrimEditorDialog::kObjectName))
            return true;
    }
    return false;
}

struct Playback {
    qlonglong position{-1};
    qlonglong duration{0};
    int state{-1};
};

bool readPlayback(QObject* sdk, Playback& playback) {
    return sdk &&
        QMetaObject::invokeMethod(sdk, "getPositionMs", Qt::DirectConnection,
                                  Q_RETURN_ARG(qlonglong, playback.position)) &&
        QMetaObject::invokeMethod(sdk, "getDurationMs", Qt::DirectConnection,
                                  Q_RETURN_ARG(qlonglong, playback.duration)) &&
        QMetaObject::invokeMethod(sdk, "GetPlayerState", Qt::DirectConnection,
                                  QGenericReturnArgument("SdkPlayer::State", &playback.state)) &&
        playback.position >= 0 && playback.duration > 0 &&
        (playback.state == 1 || playback.state == 2);
}

QString timeText(qlonglong milliseconds) {
    if (milliseconds < 0) return QStringLiteral("--");
    return QStringLiteral("%1:%2:%3.%4")
        .arg(milliseconds / 3600000, 2, 10, QLatin1Char('0'))
        .arg((milliseconds / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((milliseconds / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

QString frameText(QObject* sdk, const char* method, bool total) {
    qlonglong frames = -1;
    if (!sdk || sdk->metaObject()->indexOfMethod((QByteArray(method) + "()").constData()) < 0 ||
        !QMetaObject::invokeMethod(sdk, method, Qt::DirectConnection,
                                   Q_RETURN_ARG(qlonglong, frames)) || frames < 0 ||
        (total && frames == 0)) return QStringLiteral("--");
    return QString::number(frames);
}

} // namespace

void VideoPlaybackShortcuts::install(QWidget* previewControl) {
    if (!qApp) return;
    auto* controller = qApp->findChild<VideoPlaybackShortcuts*>(
        QStringLiteral("BillfishVideoPlaybackShortcuts"), Qt::FindDirectChildrenOnly);
    if (!controller) controller = new VideoPlaybackShortcuts(qApp);
    controller->registerPlayer(previewControl);
}

VideoPlaybackShortcuts::VideoPlaybackShortcuts(QObject* parent) : QObject(parent) {
    setObjectName(QStringLiteral("BillfishVideoPlaybackShortcuts"));
    qApp->installEventFilter(this);
    osdHideTimer_ = new QTimer(this);
    osdHideTimer_->setSingleShot(true);
    osdHideTimer_->setInterval(2000);
    connect(osdHideTimer_, &QTimer::timeout, this, &VideoPlaybackShortcuts::hideOsd);
}

VideoPlaybackShortcuts::~VideoPlaybackShortcuts() {
    delete osd_.data();
}

void VideoPlaybackShortcuts::registerPlayer(QWidget* control) {
    auto* video = control ? control->parentWidget() : nullptr;
    if (!control || !control->inherits("PreviewControlWidget") ||
        !video || !video->inherits("PreviewVideoWidget")) return;
    for (auto iterator = players_.begin(); iterator != players_.end();) {
        if (!iterator->video || !iterator->sdk) iterator = players_.erase(iterator);
        else {
            if (iterator->video == video) return;
            ++iterator;
        }
    }
    auto* page = ancestor(video, "BfLocalResWidget");
    if (!page || editing(video)) return;
    for (auto* child : video->children()) {
        if (QByteArray(child->metaObject()->className()) != QByteArrayLiteral("sdk_player::SdkPlayer"))
            continue;
        const auto* meta = child->metaObject();
        if (meta->indexOfMethod("setPosition(qlonglong)") < 0 ||
            meta->indexOfMethod("pause()") < 0 ||
            meta->indexOfMethod("frameBackward()") < 0 ||
            meta->indexOfMethod("frameForward()") < 0) return;
        players_.append({video, control, page, child});
        const int positionSignal = meta->indexOfSignal("positionChanged(qlonglong)");
        if (positionSignal >= 0) {
            QObject::connect(child, meta->method(positionSignal), this,
                metaObject()->method(metaObject()->indexOfSlot("positionChanged(qlonglong)")));
        }
        const int mediaSignal = meta->indexOfSignal("mediaStatusChanged(int)");
        if (mediaSignal >= 0) {
            QObject::connect(child, meta->method(mediaSignal), this,
                metaObject()->method(metaObject()->indexOfSlot("mediaStatusChanged(int)")));
        }
        for (const char* signature : {"signal_seekPlay(int)", "signal_forward()", "signal_rewind()"}) {
            const int signal = control->metaObject()->indexOfSignal(signature);
            if (signal >= 0) {
                QObject::connect(control, control->metaObject()->method(signal), this,
                    metaObject()->method(metaObject()->indexOfSlot("nativePlaybackOperation()")));
            }
        }
        return;
    }
}

const VideoPlaybackShortcuts::Player* VideoPlaybackShortcuts::focusedPlayer(QWidget* receiver) const {
    auto* active = QApplication::activeWindow();
    auto* focus = QApplication::focusWidget();
    if (!active || !receiver || QApplication::activePopupWidget() ||
        QApplication::activeModalWidget() || editing(receiver) || editing(focus)) return nullptr;
    if (receiver->window() != active || (focus && focus->window() != active)) return nullptr;
    auto* context = focus ? focus : receiver;
    const Player* selected = nullptr;
    for (const auto& player : players_) {
        if (!player.video || !player.sdk || !player.page || !player.video->isVisible() ||
            (player.video->window() != active && mediaSurface(player.video) != active)) continue;
        if (within(context, player.video)) return &player;
        // Native fullscreen makes the vendor MediaWidget child a window while
        // its PreviewVideoWidget remains in the ordinary page. Only that video's
        // own subtree may control it from the detached child window.
        if (player.video->window() != active) continue;
        if (ancestor(context, "BfLocalResWidget") != player.page) continue;
        // Never choose between unrelated visible previews in the same page.
        if (selected) return nullptr;
        selected = &player;
    }
    return selected;
}

void VideoPlaybackShortcuts::refreshPosition(const Player& player) {
    Playback playback;
    if (!player.control || !readPlayback(player.sdk, playback)) return;
    const int position = static_cast<int>(std::min(
        playback.position, static_cast<qlonglong>(std::numeric_limits<int>::max())));
    // This vendor UI-only slot blocks slider seek signals.
    QMetaObject::invokeMethod(player.control, "slot_updatePlayPos", Qt::DirectConnection,
                              Q_ARG(int, position));
}

void VideoPlaybackShortcuts::finishKey(int key) {
    if (lastKey_ != key || !lastPlayer_.sdk) return;
    const Player player = lastPlayer_;
    const int count = dispatchedCount_;
    lastKey_ = 0;
    dispatchedCount_ = 0;
    if (!trimDiagnosticsEnabled()) return;
    Playback playback;
    if (!readPlayback(player.sdk, playback)) return;
    trimDiagnostic(QStringLiteral("video-shortcut-release key=%1 count=%2 positionMs=%3 durationMs=%4 state=%5")
                   .arg(key).arg(count).arg(playback.position).arg(playback.duration).arg(playback.state));
}

void VideoPlaybackShortcuts::positionChanged(qlonglong positionMs) {
    Q_UNUSED(positionMs);
    if (sender() == osdPlayer_.sdk) updateOsd();
    if (!lastPlayer_.sdk || sender() != lastPlayer_.sdk) return;
    Playback playback;
    if (!readPlayback(lastPlayer_.sdk, playback) || playback.state != 2) {
        lastPlayer_ = {};
        return;
    }
    auto* focus = QApplication::focusWidget();
    auto* context = focusedPlayer(focus ? focus : QApplication::activeWindow());
    if (!context || context->sdk != lastPlayer_.sdk || !lastPlayer_.control) return;
    auto* slider = lastPlayer_.control->findChild<QAbstractSlider*>(
        QStringLiteral("PreviewControlWidget_m_pVideoSlider"));
    if (slider && slider->isSliderDown()) return;
    refreshPosition(lastPlayer_);
}

void VideoPlaybackShortcuts::mediaStatusChanged(int status) {
    if (sender() == osdPlayer_.sdk && status != 2) hideOsd();
    if (sender() == lastPlayer_.sdk && status != 2) {
        lastPlayer_ = {};
        lastKey_ = 0;
        dispatchedCount_ = 0;
    }
}

void VideoPlaybackShortcuts::nativePlaybackOperation() {
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) return;
    for (const auto& player : players_) {
        if (player.control == sender()) {
            showOsd(player);
            return;
        }
    }
}

void VideoPlaybackShortcuts::showOsd(const Player& player) {
    Playback playback;
    if (!player.video || !player.video->isVisible() || !readPlayback(player.sdk, playback)) return;
    auto* surface = mediaSurface(player.video);
    if (!surface) return;
    if (osd_ && osd_->parentWidget() != surface) {
        delete osd_.data();
    }
    if (!osd_) {
        // The vendor native video surface does not composite a Qt child backing
        // store. An owned tool window paints independently without activation.
        osd_ = new QLabel(surface, Qt::Tool | Qt::FramelessWindowHint |
                                  Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        osd_->setObjectName(QStringLiteral("BillfishVideoPlaybackOsd"));
        osd_->setAttribute(Qt::WA_DontCreateNativeAncestors);
        osd_->setAttribute(Qt::WA_TranslucentBackground);
        osd_->setAttribute(Qt::WA_NativeWindow);
        osd_->setAttribute(Qt::WA_TransparentForMouseEvents);
        osd_->setAttribute(Qt::WA_ShowWithoutActivating);
        osd_->setFocusPolicy(Qt::NoFocus);
        osd_->setTextFormat(Qt::PlainText);
        osd_->setStyleSheet(QStringLiteral(
            "QLabel { color: white; background-color: rgba(0, 0, 0, 175); "
            "padding: 8px 10px; border-radius: 4px; font-size: 14px; }"));
    }
    osdPlayer_ = player;
    osdHideTimer_->start();
    updateOsd();
    if (osdHideTimer_->isActive()) {
        osd_->show();
        osd_->raise();
    }
}

void VideoPlaybackShortcuts::updateOsd() {
    Playback playback;
    if (!osd_ || !osdHideTimer_->isActive()) return;
    if (!osdPlayer_.video || !osdPlayer_.video->isVisible() ||
        !osd_->parentWidget() || !osd_->parentWidget()->isVisible() ||
        !readPlayback(osdPlayer_.sdk, playback)) {
        hideOsd();
        return;
    }
    auto* active = QApplication::activeWindow();
    if (active != osd_->parentWidget()->window()) {
        hideOsd();
        return;
    }
    osd_->setText(QStringLiteral("当前: %1  帧: %2\n总计: %3  帧: %4")
                  .arg(timeText(playback.position))
                  .arg(frameText(osdPlayer_.sdk, "getVideoCurrentFrames", false))
                  .arg(timeText(playback.duration))
                  .arg(frameText(osdPlayer_.sdk, "getVideoTotalFrames", true)));
    osd_->adjustSize();
    osd_->move(osd_->parentWidget()->mapToGlobal(QPoint(12, 12)));
    osd_->raise();
}

void VideoPlaybackShortcuts::hideOsd() {
    osdHideTimer_->stop();
    if (osd_) osd_->hide();
    osdPlayer_ = {};
}

bool VideoPlaybackShortcuts::eventFilter(QObject* watched, QEvent* event) {
    if (osdPlayer_.video && osd_) {
        auto* widget = qobject_cast<QWidget*>(watched);
        auto* surface = osd_->parentWidget();
        const bool ownerEvent = widget && surface && within(surface, widget);
        if (event->type() == QEvent::ApplicationDeactivate ||
            (ownerEvent && (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate))) {
            hideOsd();
        } else if (ownerEvent &&
                   (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                    event->type() == QEvent::WindowStateChange || event->type() == QEvent::ParentChange)) {
            updateOsd();
        }
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress &&
        event->type() != QEvent::KeyRelease) return false;
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const int key = keyEvent->key();
    if ((key != Qt::Key_Up && key != Qt::Key_Down && key != Qt::Key_S && key != Qt::Key_D &&
         key != Qt::Key_C && key != Qt::Key_V) ||
        keyEvent->modifiers() != Qt::NoModifier) return false;
    const auto* source = focusedPlayer(qobject_cast<QWidget*>(watched));
    Playback playback;
    if (!source || !readPlayback(source->sdk, playback)) return false;
    if (event->type() == QEvent::ShortcutOverride) {
        keyEvent->accept();
        return true;
    }
    if (event->type() == QEvent::KeyRelease) {
        if (!keyEvent->isAutoRepeat()) finishKey(key);
        keyEvent->accept();
        return true;
    }
    const Player player = *source;
    if (key == Qt::Key_C || key == Qt::Key_V) {
        if (!keyEvent->isAutoRepeat()) {
            constexpr qlonglong maximumSeconds = std::numeric_limits<qlonglong>::max() / 1000;
            stepSeconds_ = key == Qt::Key_C ? std::max(1LL, stepSeconds_ - 1)
                                          : std::min(maximumSeconds, stepSeconds_ + 1);
            auto* tooltipHost = mediaSurface(player.video);
            if (!tooltipHost) tooltipHost = player.video;
            QToolTip::showText(tooltipHost->mapToGlobal(QPoint(tooltipHost->width() / 2, 24)),
                              QStringLiteral("跳转步长: %1 秒").arg(stepSeconds_), tooltipHost);
        }
        keyEvent->accept();
        return true;
    }
    bool dispatched = false;
    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        const qlonglong position = std::clamp(playback.position, 0LL, playback.duration);
        const qlonglong stepMs = stepSeconds_ * 1000;
        const qlonglong target = key == Qt::Key_Up
            ? position + std::min(stepMs, playback.duration - position)
            : position - std::min(stepMs, position);
        dispatched = QMetaObject::invokeMethod(player.sdk, "setPosition", Qt::DirectConnection,
                                               Q_ARG(qlonglong, target));
    } else {
        dispatched = QMetaObject::invokeMethod(player.sdk, "pause", Qt::DirectConnection) &&
            QMetaObject::invokeMethod(player.sdk, key == Qt::Key_S ? "frameBackward" : "frameForward",
                                      Qt::DirectConnection);
    }
    if (dispatched) {
        if (lastKey_ != key || lastPlayer_.sdk != player.sdk || !keyEvent->isAutoRepeat())
            dispatchedCount_ = 0;
        lastKey_ = key;
        lastPlayer_ = player;
        ++dispatchedCount_;
        refreshPosition(player);
        showOsd(player);
    }
    keyEvent->accept();
    return true;
}

} // namespace billfish::trim
