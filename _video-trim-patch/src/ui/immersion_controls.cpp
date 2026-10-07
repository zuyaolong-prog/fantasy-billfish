#include "ui/immersion_controls.hpp"

#include <QAbstractSlider>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGridLayout>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstring>

namespace billfish::trim {
namespace {

bool exactClass(const QWidget* widget, const char* name) {
    return widget != nullptr && std::strcmp(widget->metaObject()->className(), name) == 0;
}

bool within(const QObject* object, const QObject* ancestor) {
    for (auto current = object; current != nullptr; current = current->parent()) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

}  // namespace

void ImmersionControls::install(QWidget* previewControl) {
    if (!exactClass(previewControl, "PreviewControlWidget")) {
        return;
    }
    if (auto existing = previewControl->findChild<ImmersionControls*>(
            QString(), Qt::FindDirectChildrenOnly)) {
        existing->reconcile();
        return;
    }
    auto video = previewControl->parentWidget();
    auto stack = video != nullptr ? video->parentWidget() : nullptr;
    auto preview = stack != nullptr ? stack->parentWidget() : nullptr;
    if (!exactClass(video, "PreviewVideoWidget") ||
        !exactClass(stack, "PreviewStackWidget") || !exactClass(preview, "PreviewWidget")) {
        return;
    }
    auto grid = qobject_cast<QGridLayout*>(video->layout());
    if (grid == nullptr || grid->itemAtPosition(1, 0) == nullptr ||
        grid->itemAtPosition(1, 0)->widget() != previewControl) {
        return;
    }
    auto controller = new ImmersionControls(previewControl, video, stack, preview);
    controller->reconcile();
}

ImmersionControls::ImmersionControls(QWidget* control, QWidget* video, QWidget* stack,
                                     QWidget* preview)
    : QObject(control), control_(control), video_(video), stack_(stack), preview_(preview),
      hoverTimer_(new QTimer(this)) {
    hoverTimer_->setInterval(100);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        reconcile();
        if (active_) {
            updateHover();
        }
    });
    qApp->installEventFilter(this);
}

bool ImmersionControls::hasImmersionHierarchy() const {
    return control_ && video_ && stack_ && preview_ &&
           control_->parentWidget() == video_ && video_->parentWidget() == stack_ &&
           stack_->parentWidget() == preview_;
}

void ImmersionControls::reconcile() {
    const bool shouldBeActive = hasImmersionHierarchy() && video_->isVisible();
    if (preview_) {
        if (!previous_.button) {
            previous_.button = preview_->findChild<QWidget*>(
                QStringLiteral("materialviewer_prev_file"), Qt::FindDirectChildrenOnly);
        }
        if (!next_.button) {
            next_.button = preview_->findChild<QWidget*>(
                QStringLiteral("materialviewer_next_file"), Qt::FindDirectChildrenOnly);
        }
    }
    for (auto edge : {&previous_, &next_}) {
        if (shouldBeActive && edge->button && !edge->captured) {
            edge->originalVisible = !edge->button->isHidden();
            edge->captured = true;
            edge->revealed = false;
            edge->outsideEdge.invalidate();
        }
    }
    if (shouldBeActive == active_) {
        if (active_) {
            beginOverlay();
            positionOverlay();
            setWidgetVisible(control_, revealed_);
            setWidgetVisible(previous_.button, previous_.revealed);
            setWidgetVisible(next_.button, next_.revealed);
        } else if (!preview_ || !preview_->isVisible()) {
            // These vendor buttons are child top-level windows. Showing them
            // while their preview is hidden leaks them into the ordinary page.
            setWidgetVisible(previous_.button, false);
            setWidgetVisible(next_.button, false);
        }
        return;
    }
    active_ = shouldBeActive;
    pressedInControls_ = false;
    outsideBottom_.invalidate();
    if (active_) {
        originalVisible_ = !control_->isHidden();
        controlsHeight_ = std::max(1, control_->height());
        revealed_ = false;
        setWidgetVisible(control_, false);
        beginOverlay();
        setWidgetVisible(previous_.button, false);
        setWidgetVisible(next_.button, false);
        hoverTimer_->start();
    } else {
        hoverTimer_->stop();
        endOverlay();
        setWidgetVisible(control_, originalVisible_);
        for (auto edge : {&previous_, &next_}) {
            if (edge->captured) {
                setWidgetVisible(edge->button, preview_ && preview_->isVisible() &&
                                                   edge->originalVisible);
                edge->captured = false;
            }
        }
    }
}

void ImmersionControls::beginOverlay() {
    if (overlay_ || !hasImmersionHierarchy()) {
        return;
    }
    auto grid = qobject_cast<QGridLayout*>(video_->layout());
    const int index = grid != nullptr ? grid->indexOf(control_) : -1;
    if (index < 0) {
        return;
    }
    originalGrid_ = grid;
    grid->getItemPosition(index, &originalRow_, &originalColumn_, &originalRowSpan_,
                          &originalColumnSpan_);
    originalAlignment_ = grid->itemAt(index)->alignment();
    overlay_ = true;
    // The vendor video renders into a native child window, which covers alien
    // widgets even after raise(). Give only the control its own native surface.
    // Keep that surface for the control lifetime, including vendor fullscreen.
    control_->setAttribute(Qt::WA_DontCreateNativeAncestors);
    control_->setAttribute(Qt::WA_NativeWindow);
    // Keep both widgets under the vendor parent. Only the control stops consuming
    // a layout row; the native media surface always occupies the full video area.
    grid->removeWidget(control_);
    grid->invalidate();
    grid->activate();
    positionOverlay();
}

void ImmersionControls::endOverlay() {
    if (!overlay_) {
        return;
    }
    overlay_ = false;
    if (originalGrid_ && control_ && video_ && control_->parentWidget() == video_ &&
        video_->layout() == originalGrid_ && originalGrid_->indexOf(control_) < 0) {
        originalGrid_->addWidget(control_, originalRow_, originalColumn_, originalRowSpan_,
                                  originalColumnSpan_, originalAlignment_);
        originalGrid_->invalidate();
        originalGrid_->activate();
    }
    originalGrid_.clear();
}

void ImmersionControls::positionOverlay() {
    if (!overlay_ || !control_ || !video_) {
        return;
    }
    const QRect area = video_->contentsRect();
    const int height = std::min(controlsHeight_, area.height());
    control_->setGeometry(area.x(), area.y() + area.height() - height, area.width(), height);
    control_->raise();
}

void ImmersionControls::scheduleReconcile() {
    if (reconcilePending_) {
        return;
    }
    reconcilePending_ = true;
    QTimer::singleShot(0, this, [this] {
        reconcilePending_ = false;
        reconcile();
    });
}

void ImmersionControls::setWidgetVisible(QWidget* widget, bool visible) {
    if (!widget || widget->isHidden() == !visible) {
        return;
    }
    changingVisibility_ = true;
    widget->setVisible(visible);
    if (visible && widget == control_ && overlay_) {
        positionOverlay();
    }
    changingVisibility_ = false;
}

bool ImmersionControls::isInteracting() const {
    if (!control_) {
        return false;
    }
    if (pressedInControls_ && QApplication::mouseButtons() != Qt::NoButton) {
        return true;
    }
    if (within(QApplication::activePopupWidget(), control_)) {
        return true;
    }
    const auto sliders = control_->findChildren<QAbstractSlider*>();
    for (const auto slider : sliders) {
        if (slider->isSliderDown()) {
            return true;
        }
    }
    // The vendor volume window is a child window, not necessarily Qt::Popup.
    const auto children = control_->findChildren<QWidget*>();
    for (const auto child : children) {
        if ((child->isWindow() || exactClass(child, "SoundControlWin")) && child->isVisible()) {
            return true;
        }
    }
    return false;
}

void ImmersionControls::updateHover() {
    const QPoint globalCursor = QCursor::pos();
    const QPoint previewCursor = preview_->mapFromGlobal(globalCursor);
    const bool insidePreview = preview_->rect().contains(previewCursor);
    updateEdge(previous_, insidePreview && previewCursor.x() < 64, globalCursor);
    updateEdge(next_, insidePreview && previewCursor.x() >= preview_->width() - 64,
               globalCursor);
    const QPoint cursor = video_->mapFromGlobal(globalCursor);
    const bool atBottom = video_->rect().contains(cursor) &&
                          cursor.y() >= video_->height() - controlsHeight_ - 12;
    if (atBottom || isInteracting()) {
        outsideBottom_.invalidate();
        revealed_ = true;
        setWidgetVisible(control_, true);
        return;
    }
    if (!revealed_) {
        setWidgetVisible(control_, false);
        return;
    }
    if (!outsideBottom_.isValid()) {
        outsideBottom_.start();
    } else if (outsideBottom_.elapsed() >= 800) {
        revealed_ = false;
        setWidgetVisible(control_, false);
    }
}

void ImmersionControls::updateEdge(EdgeControl& edge, bool atEdge,
                                   const QPoint& globalCursor) {
    if (!edge.button) {
        return;
    }
    // Vendor navigation buttons can be child windows with global geometry.
    const bool onButton = edge.button->isVisible() &&
                          edge.button->rect().contains(edge.button->mapFromGlobal(globalCursor));
    if (atEdge || onButton) {
        edge.outsideEdge.invalidate();
        edge.revealed = true;
    } else if (edge.revealed) {
        if (!edge.outsideEdge.isValid()) {
            edge.outsideEdge.start();
        } else if (edge.outsideEdge.elapsed() >= 800) {
            edge.revealed = false;
        }
    }
    setWidgetVisible(edge.button, edge.revealed);
}

bool ImmersionControls::eventFilter(QObject* watched, QEvent* event) {
    if (changingVisibility_) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (active_ && within(watched, control_)) {
            pressedInControls_ = true;
        }
        break;
    case QEvent::MouseButtonRelease:
        pressedInControls_ = false;
        break;
    case QEvent::ParentAboutToChange:
        if (active_ && (watched == control_ || watched == video_)) {
            endOverlay();
        }
        break;
    case QEvent::Resize:
        if (active_ && watched == video_) {
            positionOverlay();
        }
        break;
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::ParentChange: {
        const auto widget = qobject_cast<QWidget*>(watched);
        if (widget && (widget == previous_.button || widget == next_.button ||
                       widget == control_ || widget == video_ || widget == stack_ ||
                       widget == preview_ || (video_ && widget->isAncestorOf(video_)))) {
            scheduleReconcile();
        }
        break;
    }
    default:
        break;
    }
    return false;
}

}  // namespace billfish::trim
