#include "ui/timeline_trim_overlay.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>

namespace billfish::trim {

namespace {
constexpr int kHandleHitWidth = 22;
constexpr int kHandleWidth = 16;
constexpr int kPlayheadWidth = 4;
const QColor kTimelineColor(31, 35, 41);
const QColor kSelectionColor(79, 132, 250, 42);
const QColor kSelectionEdgeColor(79, 132, 250);
const QColor kHandleColor(79, 125, 255, 220);
const QColor kHandleAccentColor(110, 151, 255, 245);
const QColor kPlayheadColor(48, 198, 238);
}  // namespace

TimelineTrimOverlay::TimelineTrimOverlay(QSlider* timeline)
    : QWidget(timeline), timeline_(timeline) {
    Q_ASSERT(timeline != nullptr);
    setObjectName(QString::fromLatin1(kObjectName));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    knownMinimum_ = timeline->minimum();
    knownMaximum_ = timeline->maximum();
    inValue_ = knownMinimum_;
    outValue_ = knownMaximum_;
    timeline->installEventFilter(this);
    connect(timeline, &QSlider::rangeChanged, this, [this](const int minimum, const int maximum) {
        const bool selectedWholeRange = inValue_ == knownMinimum_ && outValue_ == knownMaximum_;
        knownMinimum_ = minimum;
        knownMaximum_ = maximum;
        if (selectedWholeRange) {
            setSelection(minimum, maximum);
        } else {
            setSelection(std::clamp(inValue_, minimum, maximum),
                         std::clamp(outValue_, minimum, maximum));
        }
        update();
    });
    connect(timeline, &QSlider::valueChanged, this, [this] { update(); });
    syncGeometry();
    show();
    raise();
}

QSlider* TimelineTrimOverlay::timeline() const noexcept { return timeline_; }
int TimelineTrimOverlay::inValue() const noexcept { return inValue_; }
int TimelineTrimOverlay::outValue() const noexcept { return outValue_; }

void TimelineTrimOverlay::resetToFullRange() {
    if (timeline_ != nullptr) {
        setSelection(timeline_->minimum(), timeline_->maximum());
    }
}

void TimelineTrimOverlay::setSelection(int inValue, int outValue) {
    if (timeline_ == nullptr) {
        return;
    }
    inValue = std::clamp(inValue, timeline_->minimum(), timeline_->maximum());
    outValue = std::clamp(outValue, timeline_->minimum(), timeline_->maximum());
    if (inValue > outValue) {
        std::swap(inValue, outValue);
    }
    if (inValue_ == inValue && outValue_ == outValue) {
        return;
    }
    inValue_ = inValue;
    outValue_ = outValue;
    update();
    emit selectionChanged(inValue_, outValue_);
}

void TimelineTrimOverlay::setInValue(const int value) {
    setSelection(std::min(value, outValue_), outValue_);
}

void TimelineTrimOverlay::setOutValue(const int value) {
    setSelection(inValue_, std::max(value, inValue_));
}

bool TimelineTrimOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched != timeline_ || timeline_ == nullptr) {
        return QWidget::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::StyleChange:
    case QEvent::LayoutDirectionChange:
        syncGeometry();
        update();
        return false;
    case QEvent::Paint:
    case QEvent::UpdateRequest:
        update();
        return false;
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const Handle hit = handleAt(mouse->pos());
        if (hit == Handle::None) {
            return false;
        }
        activeHandle_ = hit;
        dragging_ = true;
        timeline_->grabMouse();
        moveActiveHandle(valueForPosition(mouse->pos().x()));
        emit handlePressed();
        return true;
    }
    case QEvent::MouseMove: {
        if (!dragging_) {
            return false;
        }
        auto* mouse = static_cast<QMouseEvent*>(event);
        moveActiveHandle(valueForPosition(mouse->pos().x()));
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!dragging_ || mouse->button() != Qt::LeftButton) {
            return false;
        }
        moveActiveHandle(valueForPosition(mouse->pos().x()));
        dragging_ = false;
        timeline_->releaseMouse();
        emit handleReleased();
        return true;
    }
    case QEvent::UngrabMouse:
        if (dragging_) {
            dragging_ = false;
            emit handleReleased();
        }
        return false;
    default:
        return false;
    }
}

void TimelineTrimOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    if (timeline_ == nullptr || timeline_->orientation() != Qt::Horizontal) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), kTimelineColor);

    const int inPosition = positionForValue(inValue_);
    const int outPosition = positionForValue(outValue_);

    const int selectionLeft = std::min(inPosition, outPosition);
    const int selectionRight = std::max(inPosition, outPosition);
    QRect selectionRect(QPoint(selectionLeft, 0), QPoint(selectionRight, height() - 1));
    painter.fillRect(selectionRect, kSelectionColor);
    painter.fillRect(QRect(selectionLeft, 0,
                           std::max(1, selectionRight - selectionLeft + 1), 2),
                     kSelectionEdgeColor);

    const auto drawHandle = [&](const int x) {
        const QRect handleRect(x - kHandleWidth / 2, 0, kHandleWidth, height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(kHandleColor);
        painter.drawRoundedRect(handleRect, 2, 2);
        painter.fillRect(QRect(x - 1, 4, 2, std::max(0, height() - 8)),
                         kHandleAccentColor);
    };
    drawHandle(inPosition);
    drawHandle(outPosition);

    const int playheadPosition = positionForValue(timeline_->value());
    painter.setPen(Qt::NoPen);
    painter.setBrush(kPlayheadColor);
    painter.drawRoundedRect(
        QRect(playheadPosition - kPlayheadWidth / 2, 3, kPlayheadWidth,
              std::max(0, height() - 6)),
        2, 2);
}

bool TimelineTrimOverlay::isUpsideDown() const noexcept {
    if (timeline_ == nullptr) {
        return false;
    }
    bool upsideDown = timeline_->orientation() == Qt::Vertical;
    if (timeline_->orientation() == Qt::Horizontal && timeline_->layoutDirection() == Qt::RightToLeft) {
        upsideDown = true;
    }
    if (timeline_->invertedAppearance()) {
        upsideDown = !upsideDown;
    }
    return upsideDown;
}

QRect TimelineTrimOverlay::grooveRect() const {
    if (timeline_ == nullptr) {
        return {};
    }
    QStyleOptionSlider option;
    option.initFrom(timeline_);
    option.orientation = timeline_->orientation();
    option.minimum = timeline_->minimum();
    option.maximum = timeline_->maximum();
    option.sliderPosition = timeline_->sliderPosition();
    option.sliderValue = timeline_->value();
    option.upsideDown = isUpsideDown();
    return timeline_->style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, timeline_);
}

QRect TimelineTrimOverlay::stockHandleRect() const {
    if (timeline_ == nullptr) {
        return {};
    }
    QStyleOptionSlider option;
    option.initFrom(timeline_);
    option.orientation = timeline_->orientation();
    option.minimum = timeline_->minimum();
    option.maximum = timeline_->maximum();
    option.sliderPosition = timeline_->sliderPosition();
    option.sliderValue = timeline_->value();
    option.upsideDown = isUpsideDown();
    return timeline_->style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, timeline_);
}

int TimelineTrimOverlay::positionForValue(const int value) const {
    if (timeline_ == nullptr) {
        return 0;
    }
    const QRect groove = grooveRect();
    const int handleLength = std::max(1, stockHandleRect().width());
    const int sliderMinimum = groove.x();
    const int span = std::max(0, groove.width() - handleLength);
    return sliderMinimum + handleLength / 2 + QStyle::sliderPositionFromValue(
               timeline_->minimum(), timeline_->maximum(), value, span, isUpsideDown());
}

int TimelineTrimOverlay::valueForPosition(const int position) const {
    if (timeline_ == nullptr) {
        return 0;
    }
    const QRect groove = grooveRect();
    const int handleLength = std::max(1, stockHandleRect().width());
    const int span = std::max(1, groove.width() - handleLength);
    const int sliderPosition = std::clamp(position - groove.x() - handleLength / 2, 0, span);
    return QStyle::sliderValueFromPosition(
        timeline_->minimum(), timeline_->maximum(), sliderPosition, span, isUpsideDown());
}

QRect TimelineTrimOverlay::handleHitRect(const Handle handle) const {
    const int x = positionForValue(handle == Handle::In ? inValue_ : outValue_);
    return QRect(x - kHandleHitWidth / 2, 0, kHandleHitWidth, height());
}

TimelineTrimOverlay::Handle TimelineTrimOverlay::handleAt(const QPoint& position) const {
    const bool atIn = handleHitRect(Handle::In).contains(position);
    const bool atOut = handleHitRect(Handle::Out).contains(position);
    if (atIn && !atOut) {
        return Handle::In;
    }
    if (atOut && !atIn) {
        return Handle::Out;
    }
    if (atIn && atOut) {
        return activeHandle_ == Handle::Out ? Handle::Out : Handle::In;
    }
    return Handle::None;
}

void TimelineTrimOverlay::moveActiveHandle(const int value) {
    if (activeHandle_ == Handle::In) {
        setInValue(value);
    } else if (activeHandle_ == Handle::Out) {
        setOutValue(value);
    }
}

void TimelineTrimOverlay::syncGeometry() {
    if (timeline_ == nullptr) {
        return;
    }
    setGeometry(timeline_->rect());
    raise();
}

}  // namespace billfish::trim
