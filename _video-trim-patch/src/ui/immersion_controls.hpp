#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class QWidget;
class QTimer;
class QPoint;
class QGridLayout;

namespace billfish::trim {

class ImmersionControls final : public QObject {
    Q_OBJECT

public:
    static void install(QWidget* previewControl);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct EdgeControl;
    explicit ImmersionControls(QWidget* control, QWidget* video, QWidget* stack,
                               QWidget* preview);
    void reconcile();
    void scheduleReconcile();
    void updateHover();
    void updateEdge(EdgeControl& edge, bool atEdge, const QPoint& globalCursor);
    void setWidgetVisible(QWidget* widget, bool visible);
    void beginOverlay();
    void endOverlay();
    void positionOverlay();
    bool hasImmersionHierarchy() const;
    bool isInteracting() const;

    QPointer<QWidget> control_;
    QPointer<QWidget> video_;
    QPointer<QWidget> stack_;
    QPointer<QWidget> preview_;
    QPointer<QGridLayout> originalGrid_;
    int originalRow_{1};
    int originalColumn_{0};
    int originalRowSpan_{1};
    int originalColumnSpan_{1};
    Qt::Alignment originalAlignment_{};
    bool overlay_{false};
    QTimer* hoverTimer_;
    QElapsedTimer outsideBottom_;
    struct EdgeControl {
        QPointer<QWidget> button;
        QElapsedTimer outsideEdge;
        bool captured{false};
        bool originalVisible{true};
        bool revealed{false};
    };
    EdgeControl previous_;
    EdgeControl next_;
    int controlsHeight_{78};
    bool active_{false};
    bool originalVisible_{true};
    bool revealed_{false};
    bool changingVisibility_{false};
    bool reconcilePending_{false};
    bool pressedInControls_{false};
};

}  // namespace billfish::trim
