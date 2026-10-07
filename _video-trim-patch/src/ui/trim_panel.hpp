#pragma once

#include "core/video_trim_core.hpp"

#include <QPointer>
#include <QWidget>
#include <QList>
#include <QStringList>

class QAbstractButton;
class QLabel;
class QProgressBar;
class QSlider;
class QThread;
class QToolButton;

namespace billfish::trim {

class TimelineTrimOverlay;
class TrimExportWorker;
class MediaProbeWorker;

class TrimPanel final : public QWidget {
    Q_OBJECT

public:
    static constexpr auto kObjectName = "BillfishVideoTrimPanel";

    explicit TrimPanel(QWidget* parent = nullptr);
    ~TrimPanel() override;

    [[nodiscard]] TimelineTrimOverlay* timelineOverlay() const noexcept;
    [[nodiscard]] RangeSelection selection() const noexcept;
    [[nodiscard]] TrimMode trimMode() const noexcept;
    void setTrimMode(TrimMode mode);
    [[nodiscard]] bool isPreviewing() const noexcept;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] QSlider* hostTimeline() const noexcept;
    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] qint64 mediaDurationMs() const noexcept;
    [[nodiscard]] qint64 currentPlaybackPositionMs() const noexcept;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    bool attachToPlayer(QWidget* playerRoot, QObject* selectedFileIdSource = nullptr,
                        qint64 observedFileId = -1);
    [[nodiscard]] bool refreshRuntimeBindings(QWidget* playerRoot,
                                              QObject* selectedFileIdSource = nullptr,
                                              qint64 observedFileId = -1,
                                              QWidget* previewControl = nullptr,
                                              bool diagnoseBindingFailure = false);
    void applyVerifiedSource(const QString& path, qint64 durationMs);
    bool exportToPath(const QString& outputPath);
    bool copySelectionToClipboard();
    void stopPreview();
    void cancelExport();

signals:
    void selectionChanged(qint64 startMs, qint64 endMs);
    void sourceChanged(const QString& sourcePath, qint64 durationMs);
    void previewStarted(qint64 startMs, qint64 endMs);
    void previewStopped();
    void operationProgress(int percent, const QString& stage);
    void exportFinished(const QString& outputPath);
    void copyFinished(const QString& outputPath);
    void operationFailed(const QString& error);

private slots:
    void setInPointFromPlayhead();
    void setOutPointFromPlayhead();
    void togglePreview(bool enabled);
    void chooseExportPath();
    void onTimelineValueChanged(int value);
    void onOverlaySelectionChanged(int inValue, int outValue);
    void onPreviewFileIdChanged(qint64 fileId);
    void onVideoFramesChanged(qint64 secondFrames, qint64 currentFrames);
    void onVideoTotalFramesChanged(qint64 totalFrames, qint64 lastSecondFrames);
    void onWorkerProgress(int stage, qint64 processedMs, qint64 totalMs, int percent);
    void onWorkerCompleted(bool success, const QString& outputPath, const QString& error,
                           qint64 elapsedMs);
    void onProbeCompleted(const QString& inputPath, bool success, qint64 durationMs,
                          const QString& error);

private:
    enum class JobPurpose { None, Export, Copy };

    void updateLabels();
    void updateActionState();
    void updatePreviewButtonVisual();
    void applyCompactStyle();
    void seekHostTimeline(int value);
    void clickHostButton(QAbstractButton* button);
    void connectFileIdSource(QWidget* playerRoot);
    void connectPlaybackFrameSources(QWidget* playerRoot);
    void diagnoseMissingFileIdEmitter(QWidget* playerRoot,
                                      const QList<QObject*>& typedCandidates,
                                      int rawSignalObjectCount,
                                      int rawSignalMethodCount,
                                      int typedSignalMethodCount,
                                      bool connectAttempted,
                                      bool connectSucceeded,
                                      int receiverSlotIndex,
                                      const QString& receiverSlotSignature) const;
    bool synchronizeCurrentSource();
    bool resolveFileId(qint64 fileId);
    void invalidateResolvedSource(const QString& error);
    void setResolvedSource(const QString& path, qint64 durationMs);
    void startDurationProbe();
    void setStatus(const QString& text, bool error = false);
    void setBusy(bool busy, const QString& text = {});
    bool startTrim(TrimRequest request, JobPurpose purpose);
    bool copyFileToClipboard(const QString& outputPath);
    [[nodiscard]] TrimRequest makeRequest(const QString& outputPath) const;
    [[nodiscard]] QString defaultExportPath() const;
    [[nodiscard]] QString cacheRoot() const;
    [[nodiscard]] qint64 timelineValueToMs(int value) const noexcept;
    [[nodiscard]] static QSlider* findHostTimeline(QWidget* playerRoot);
    [[nodiscard]] static QAbstractButton* findButton(QWidget* playerRoot, const QString& objectName);

    QPointer<QWidget> playerRoot_;
    QPointer<QSlider> hostTimeline_;
    QPointer<QAbstractButton> hostPlayButton_;
    QPointer<QAbstractButton> hostPauseButton_;
    QPointer<QWidget> previewControl_;
    QPointer<QObject> selectedFileIdSource_;
    qint64 selectedObservedFileId_{-1};
    QPointer<QObject> fileIdEmitter_;
    QList<QPointer<QObject>> playbackFrameSources_;
    QPointer<TimelineTrimOverlay> overlay_;
    QPointer<TrimExportWorker> worker_;
    QPointer<QThread> workerThread_;
    QPointer<QThread> probeThread_;
    QLabel* sourceLabel_{nullptr};
    QLabel* inLabel_{nullptr};
    QLabel* outLabel_{nullptr};
    QLabel* durationLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QToolButton* modeButton_{nullptr};
    QToolButton* setInButton_{nullptr};
    QToolButton* setOutButton_{nullptr};
    QToolButton* previewButton_{nullptr};
    QToolButton* exportButton_{nullptr};
    QToolButton* copyButton_{nullptr};
    QToolButton* cancelButton_{nullptr};
    QProgressBar* progressBar_{nullptr};
    QString sourcePath_;
    QString probingSourcePath_;
    QString sourceResolutionError_;
    qint64 mediaDurationMs_{-1};
    qint64 currentFileId_{-1};
    qint64 lastObservedFileId_{-1};
    qint64 currentVideoFrame_{-1};
    qint64 totalVideoFrames_{-1};
    TrimRequest activeRequest_;
    JobPurpose activePurpose_{JobPurpose::None};
    bool explicitInputOverride_{false};
    TrimMode selectedMode_{TrimMode::AccurateReencode};
    bool previewing_{false};
    bool wrappingPreview_{false};
    bool busy_{false};
    QString diagnosticRefreshSignature_;
    bool diagnoseBindingFailure_{false};
};

}  // namespace billfish::trim
