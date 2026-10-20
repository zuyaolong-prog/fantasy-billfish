#pragma once

#include <QPointer>
#include <QWidget>

class QMouseEvent;
class QSlider;

namespace billfish::trim {

// Paints and edits the trim selection directly on the stock playback slider.
// The stock QSlider remains alive and receives every non-handle interaction,
// preserving its value, range, signals, connections, and scrub semantics.
class TimelineTrimOverlay final : public QWidget {
    Q_OBJECT

public:
    static constexpr auto kObjectName = "BillfishVideoTrimTimelineOverlay";

    explicit TimelineTrimOverlay(QSlider* timeline);

    [[nodiscard]] QSlider* timeline() const noexcept;
    [[nodiscard]] int inValue() const noexcept;
    [[nodiscard]] int outValue() const noexcept;

    void resetToFullRange();
    void setSelection(int inValue, int outValue);
    void setInValue(int value);
    void setOutValue(int value);

signals:
    void selectionChanged(int inValue, int outValue);
    void handlePressed();
    void handleReleased();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class Handle { None, In, Out };

    [[nodiscard]] bool isUpsideDown() const noexcept;
    [[nodiscard]] QRect grooveRect() const;
    [[nodiscard]] QRect stockHandleRect() const;
    [[nodiscard]] int positionForValue(int value) const;
    [[nodiscard]] int valueForPosition(int position) const;
    [[nodiscard]] QRect handleHitRect(Handle handle) const;
    [[nodiscard]] Handle handleAt(const QPoint& position) const;
    void moveActiveHandle(int value);
    void syncGeometry();

    QPointer<QSlider> timeline_;
    int inValue_{0};
    int outValue_{0};
    int knownMinimum_{0};
    int knownMaximum_{0};
    Handle activeHandle_{Handle::None};
    bool dragging_{false};
    bool ownsMouseGrab_{false};
};

}  // namespace billfish::trim
