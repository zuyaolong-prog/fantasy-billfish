#include "ui/immersion_playback.hpp"
#include "ui/trim_diagnostics.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QShortcutEvent>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace billfish::trim {
namespace {
QWidget* ancestor(QWidget* widget, const char* name) {
    for (auto* current = widget; current; current = current->parentWidget()) {
        if (current->inherits(name)) return current;
    }
    return nullptr;
}

QObject* videoSdk(QWidget* video) {
    for (auto* child : video->children()) {
        if (QByteArray(child->metaObject()->className()) == QByteArrayLiteral("sdk_player::SdkPlayer"))
            return child;
    }
    return nullptr;
}
} // namespace

void ImmersionPlayback::install(QWidget* control) {
    auto* controller = qApp->findChild<ImmersionPlayback*>(
        QStringLiteral("BillfishImmersionPlayback"), Qt::FindDirectChildrenOnly);
    if (!controller) controller = new ImmersionPlayback(qApp);
    controller->registerPlayer(control);
}

ImmersionPlayback::ImmersionPlayback(QObject* parent) : QObject(parent) {
    setObjectName(QStringLiteral("BillfishImmersionPlayback"));
    qApp->installEventFilter(this);
}

void ImmersionPlayback::registerPlayer(QWidget* control) {
    auto* video = control ? control->parentWidget() : nullptr;
    if (!video || !video->inherits("PreviewVideoWidget")) return;
    auto* details = ancestor(video, "PreviewDetailsWidget");
    auto* immersion = ancestor(video, "PreviewWidget");
    auto* page = ancestor(video, "BfLocalResWidget");
    auto* sdk = videoSdk(video);
    if ((!details && !immersion) || !page || !sdk) return;
    for (auto iterator = players_.begin(); iterator != players_.end();) {
        if (!iterator->video || !iterator->sdk) iterator = players_.erase(iterator);
        else {
            if (iterator->video == video) return;
            ++iterator;
        }
    }
    const int signal = sdk->metaObject()->indexOfSignal("mediaStatusChanged(int)");
    if (signal < 0) return;
    QObject::connect(sdk, sdk->metaObject()->method(signal), this,
        metaObject()->method(metaObject()->indexOfSlot("mediaStatusChanged(int)")));
    players_.append({video, control, page, sdk, immersion != nullptr});

    // The immersive SDK can first be discovered after its initial load is ready.
    if (pending_.page == page && pending_.immersion == (immersion != nullptr) && !pending_.sdk) {
        pending_.sdk = sdk;
        pending_.video = video;
        pending_.control = control;
        int status = -1;
        if (QMetaObject::invokeMethod(sdk, "GetMediaStatus", Qt::DirectConnection,
                                     QGenericReturnArgument("SdkPlayer::MediaStatus", &status))) {
            pending_.loading = status == 1 || status == 2;
            if (status == 2) {
                const quint64 generation = generation_;
                QTimer::singleShot(0, this, [this, generation] { restore(generation); });
            }
        }
    }

    auto* host = immersion ? immersion : details;
    const QString name = immersion ? QStringLiteral("materialviewer_exit_imm") : QStringLiteral("ImmersionBtn");
    if (auto* button = host->findChild<QAbstractButton*>(name)) {
        connect(button, &QAbstractButton::pressed, this, [this, button, immersion] {
            capture(button, immersion == nullptr);
        });
    }
}

void ImmersionPlayback::capture(QWidget* trigger, bool entering) {
    pending_ = {};
    ++generation_;
    auto* page = ancestor(trigger, "BfLocalResWidget");
    const Player* source = nullptr;
    const Player* target = nullptr;
    for (const auto& player : players_) {
        if (!player.video || !player.sdk || player.page != page) continue;
        if (player.immersion == entering) target = &player;
        else if (player.video->isVisible()) source = &player;
    }
    if (!source || source->video->window() != source->page->window()) return;
    qlonglong position = -1, duration = 0;
    int state = -1;
    if (!QMetaObject::invokeMethod(source->sdk, "getPositionMs", Qt::DirectConnection,
                                  Q_RETURN_ARG(qlonglong, position)) ||
        !QMetaObject::invokeMethod(source->sdk, "getDurationMs", Qt::DirectConnection,
                                  Q_RETURN_ARG(qlonglong, duration)) ||
        !QMetaObject::invokeMethod(source->sdk, "GetPlayerState", Qt::DirectConnection,
                                  QGenericReturnArgument("SdkPlayer::State", &state)) ||
        position < 0 || duration <= 0 || (state != 1 && state != 2)) return;
    pending_.sdk = target ? target->sdk : nullptr;
    pending_.video = target ? target->video : nullptr;
    pending_.control = target ? target->control : nullptr;
    pending_.positionMs = position;
    pending_.durationMs = duration;
    pending_.playing = state == 1;
    pending_.page = page;
    pending_.immersion = entering;
    trimDiagnostic(QStringLiteral("immersion-capture entering=%1 positionMs=%2 playing=%3")
                   .arg(entering).arg(position).arg(state == 1));
}

void ImmersionPlayback::mediaStatusChanged(int status) {
    if (!pending_.sdk || sender() != pending_.sdk) return;
    if (status == 1) {
        // A second load is a different navigation before the first one finished.
        if (pending_.loading) { pending_ = {}; return; }
        pending_.loading = true;
    } else if (status == 2 && pending_.loading) {
        const quint64 generation = generation_;
        // The frozen SDK emits Ready immediately before its automatic play().
        // Restore after that callback so the saved paused state is not overwritten.
        QTimer::singleShot(0, this, [this, generation] { restore(generation); });
    } else if (status != 2) {
        pending_ = {};
    }
}

void ImmersionPlayback::restore(quint64 generation) {
    if (generation != generation_ || !pending_.sdk || !pending_.video) return;
    const Transfer transfer = pending_;
    pending_ = {};
    qlonglong duration = 0;
    if (!transfer.video->isVisible() ||
        !QMetaObject::invokeMethod(transfer.sdk, "getDurationMs", Qt::DirectConnection,
                                   Q_RETURN_ARG(qlonglong, duration)) || duration != transfer.durationMs) return;
    const qlonglong position = std::clamp(transfer.positionMs, 0LL, duration);
    const bool seeked = QMetaObject::invokeMethod(transfer.sdk, "setPosition", Qt::DirectConnection,
                                                  Q_ARG(qlonglong, position));
    const bool stateSet = QMetaObject::invokeMethod(transfer.sdk, transfer.playing ? "play" : "pause",
                                                    Qt::DirectConnection);
    if (seeked && !transfer.playing && transfer.control) {
        // Stock refreshes the timeline only while playing. Its UI-only slot
        // blocks slider signals, so this also refreshes a paused transfer safely.
        QMetaObject::invokeMethod(transfer.control, "slot_updatePlayPos", Qt::DirectConnection,
                                  Q_ARG(int, static_cast<int>(position)));
    }
    trimDiagnostic(QStringLiteral("immersion-restore positionMs=%1 playing=%2 dispatched=%3")
                   .arg(position).arg(transfer.playing).arg(seeked && stateSet));
}

bool ImmersionPlayback::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Shortcut && !QApplication::activePopupWidget() &&
        !QApplication::activeModalWidget()) {
        const auto* shortcut = static_cast<QShortcutEvent*>(event);
        if (!shortcut->isAmbiguous() && pending_.page &&
            (shortcut->key() == QKeySequence(Qt::Key_Left) ||
             shortcut->key() == QKeySequence(Qt::Key_Right))) {
            auto* owner = watched->parent();
            while (owner && owner != pending_.page) owner = owner->parent();
            if (owner) pending_ = {};
        }
        if (!shortcut->isAmbiguous() && shortcut->key() == QKeySequence(Qt::Key_Escape)) {
            QList<QWidget*> owners;
            if (auto* action = qobject_cast<QAction*>(watched)) owners = action->associatedWidgets();
            for (auto* parent = watched->parent(); parent; parent = parent->parent()) {
                if (auto* owner = qobject_cast<QWidget*>(parent)) {
                    owners.append(owner);
                    break;
                }
            }
            for (const auto& player : players_) {
                if (!player.immersion || !player.video || !player.video->isVisible()) continue;
                const bool ownsPlayer = std::any_of(owners.cbegin(), owners.cend(), [&player](QWidget* owner) {
                    return owner == player.video || owner->isAncestorOf(player.video);
                });
                if (ownsPlayer) {
                    // QAction consumes Escape before a QWidget KeyPress arrives.
                    // Capture before its vendor slot unloads the immersive SDK.
                    capture(player.video, false);
                    break;
                }
            }
        }
        return false;
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return false;
    if (event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton &&
        !QApplication::activePopupWidget() && !QApplication::activeModalWidget()) {
        for (const auto& player : players_) {
            if (!player.immersion || !player.video || !player.page || !player.video->isVisible() ||
                widget->window() != player.page->window()) continue;
            if (widget == player.video ||
                (widget->inherits("MediaWidget") &&
                 ancestor(widget, "PreviewVideoWidget") == player.video)) {
                // Immersive video double-click returns to the ordinary preview.
                // Ordinary video double-click returns to the grid instead.
                capture(player.video, false);
                break;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        auto* button = qobject_cast<QAbstractButton*>(widget);
        if (button && button->isDown() && button->rect().contains(static_cast<QMouseEvent*>(event)->pos())) {
            if (button->objectName() == QStringLiteral("ImmersionBtn")) capture(button, true);
            else if (button->objectName() == QStringLiteral("materialviewer_exit_imm")) capture(button, false);
        }
    }
    if (event->type() == QEvent::KeyRelease &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space) {
        auto* button = qobject_cast<QAbstractButton*>(widget);
        if (button && button->isDown()) {
            if (button->objectName() == QStringLiteral("ImmersionBtn")) capture(button, true);
            else if (button->objectName() == QStringLiteral("materialviewer_exit_imm")) capture(button, false);
        }
    }
    if (event->type() == QEvent::MouseButtonPress &&
        (widget->objectName() == QStringLiteral("materialviewer_prev_file") ||
         widget->objectName() == QStringLiteral("materialviewer_next_file"))) {
        pending_ = {};
    }
    if (event->type() == QEvent::KeyPress && !QApplication::activePopupWidget() &&
        !QApplication::activeModalWidget()) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape) {
            for (const auto& player : players_) {
                if (player.immersion && player.video && player.video->isVisible() && player.page &&
                    widget->window() == player.page->window()) {
                    capture(player.video, false);
                    break;
                }
            }
        } else if (key == Qt::Key_Left || key == Qt::Key_Right) {
            pending_ = {};
        }
    }
    return false;
}

} // namespace billfish::trim
