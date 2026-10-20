#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QObject>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <functional>

class QTimer;
class QAction;
class QWidget;

namespace billfish::trim {

class TrimPanel;
class TrimEditorDialog;

class PlayerTrimInjector final : public QObject {
    Q_OBJECT

public:
    static constexpr auto kPlayerRootProperty = "billfish.trim.playerRoot";
    static constexpr auto kInjectedProperty = "billfish.trim.injected";
    static constexpr auto kResolverObjectName = "BillfishVideoTrimSourceResolver";
    static constexpr auto kOpenActionObjectName = "BillfishVideoTrimOpenAction";
    static constexpr auto kOpenButtonObjectName = "BillfishVideoTrimOpenButton";
    static constexpr int kScanDebounceMs = 150;
    static constexpr int kMinimumScanIntervalMs = 1000;

    explicit PlayerTrimInjector(QObject* pluginInstance, QObject* parent = nullptr,
                                std::function<QString()> currentLibraryRoot = {});

    void start();
    [[nodiscard]] bool tryInject(QWidget* searchRoot);
    [[nodiscard]] static QWidget* findPlayerRoot(QWidget* searchRoot);
    [[nodiscard]] quint64 scanPassCount() const noexcept { return scanPassCount_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void scanTopLevelWidgets();
    void onObservedPreviewFileIdChanged(qint64 fileId);

private:
    void scheduleScan();
    void discoverPreviewFileIdSources();
    void observePreviewFileIdSource(QObject* object);
    [[nodiscard]] static bool isPreviewFileIdSource(QObject* object);
    [[nodiscard]] QList<QObject*> visiblePreviewFileIdSources() const;
    [[nodiscard]] qint64 observedFileId(QObject* source) const;
    void openEditor(QWidget* playerRoot, QWidget* previewControl, TrimPanel* resolver);
    [[nodiscard]] static QList<QWidget*> findPreviewControls(QWidget* searchRoot);
    [[nodiscard]] static QWidget* findPreviewHost(QWidget* previewControl);
    [[nodiscard]] QAction* installOpenEntry(QWidget* playerRoot, QWidget* previewControl,
                                            TrimPanel* resolver);

    QPointer<QObject> pluginInstance_;
    std::function<QString()> currentLibraryRoot_;
    QPointer<TrimEditorDialog> editorDialog_;
    QTimer* scanTimer_{nullptr};
    QElapsedTimer lastScan_;
    quint64 scanPassCount_{0};
    bool started_{false};
    bool scanInProgress_{false};
    bool rescanRequested_{false};
    struct FileIdObservation final {
        QPointer<QObject> source;
        qint64 lastId{-1};
    };
    QList<FileIdObservation> fileIdObservations_;
};

void installPlayerTrimInjection(QObject* pluginInstance,
                                std::function<QString()> currentLibraryRoot);

}  // namespace billfish::trim
