#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QtGlobal>

class QLabel;
class QCloseEvent;
class QSlider;
class QTimer;
class QToolButton;
class QWidget;

namespace billfish::trim {

class MpvProcessBackend;
class TrimPanel;

class TrimEditorDialog final : public QDialog {
    Q_OBJECT

public:
    static constexpr auto kObjectName = "BillfishVideoTrimEditorDialog";
    static constexpr auto kEditorHostProperty = "billfish.trim.editorHost";
    static constexpr int kTimelineResolution = 1'000'000;

    explicit TrimEditorDialog(QWidget* parent = nullptr);
    ~TrimEditorDialog() override;

    void setSource(const QString& sourcePath, qint64 durationMs,
                   qint64 initialPositionMs = 0);

    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] qint64 mediaDurationMs() const noexcept;
    [[nodiscard]] TrimPanel* trimPanel() const noexcept;
    [[nodiscard]] QSlider* timeline() const noexcept;
    [[nodiscard]] QWidget* videoHost() const noexcept;
    [[nodiscard]] MpvProcessBackend* playbackBackend() const noexcept;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void scrubSeekRequested(qint64 positionMs);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    [[nodiscard]] int timelineValueForMs(qint64 positionMs) const noexcept;
    [[nodiscard]] qint64 positionMsForTimelineValue(int value) const noexcept;
    void applyInitialPosition();
    void requestPlaybackSeek(qint64 positionMs);
    void loadSourceInPlaybackBackend();
    void finishPendingClose();
    void showPlaybackError(const QString& error);

    QWidget* videoHost_{nullptr};
    QLabel* videoStatusLabel_{nullptr};
    QSlider* timeline_{nullptr};
    QToolButton* hostPlayButton_{nullptr};
    QToolButton* hostPauseButton_{nullptr};
    TrimPanel* trimPanel_{nullptr};
    MpvProcessBackend* playbackBackend_{nullptr};
    QTimer* scrubSeekTimer_{nullptr};
    QString sourcePath_;
    qint64 mediaDurationMs_{-1};
    qint64 initialPositionMs_{0};
    qint64 pendingScrubPositionMs_{-1};
    bool initialPositionApplied_{false};
    bool resumeAfterScrub_{false};
    bool pendingClose_{false};
};

}  // namespace billfish::trim
