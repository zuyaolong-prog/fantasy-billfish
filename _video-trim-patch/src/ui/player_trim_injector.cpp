#include "ui/player_trim_injector.hpp"
#include "automation/app_input_scope.hpp"

#include <utility>

#include "ui/trim_editor_dialog.hpp"
#include "ui/trim_diagnostics.hpp"
#include "ui/trim_panel.hpp"
#include "ui/immersion_controls.hpp"
#include "ui/immersion_playback.hpp"
#include "ui/video_playback_shortcuts.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QChildEvent>
#include <QCoreApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QFileInfo>
#include <QKeySequence>
#include <QLayout>
#include <QMenu>
#include <QMetaMethod>
#include <QMetaType>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace billfish::trim {

namespace {
constexpr auto kPreviewControlClass = "PreviewControlWidget";
constexpr auto kTimelineObjectName = "PreviewControlWidget_m_pVideoSlider";
constexpr auto kFrameButtonObjectName = "PreviewControlWidget_frame_btn";

bool propertyIsTrue(const QObject* object, const char* propertyName) {
    return object != nullptr && object->property(propertyName).toBool();
}

bool isHorizontal(const QBoxLayout* layout) {
    return layout->direction() == QBoxLayout::LeftToRight ||
           layout->direction() == QBoxLayout::RightToLeft;
}

bool isTrimPatchObject(const QObject* object) {
    const QObject* current = object;
    while (current != nullptr) {
        if (current->objectName().startsWith(QStringLiteral("BillfishVideoTrim")) ||
            qobject_cast<const TrimPanel*>(current) != nullptr) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

bool runtimeRelevant(const QObject* object) {
    if (object == nullptr) {
        return false;
    }
    const QString identity = QString::fromLatin1(object->metaObject()->className()) +
                             QLatin1Char(' ') + object->objectName();
    return identity.contains(QStringLiteral("Preview"), Qt::CaseInsensitive) ||
           identity.contains(QStringLiteral("MediaSlider"), Qt::CaseInsensitive) ||
           identity.contains(QStringLiteral("FileId"), Qt::CaseInsensitive);
}

bool isPreviewControlEvidence(QWidget* widget) {
    if (widget == nullptr) {
        return false;
    }
    const auto timeline = widget->findChildren<QSlider*>(
        QString::fromLatin1(kTimelineObjectName), Qt::FindChildrenRecursively);
    if (timeline.isEmpty()) {
        return false;
    }
    return widget->findChild<QAbstractButton*>(QString::fromLatin1(kFrameButtonObjectName),
                                               Qt::FindChildrenRecursively) != nullptr ||
           widget->findChild<QAbstractButton*>(QStringLiteral("videoFrameBtn"),
                                               Qt::FindChildrenRecursively) != nullptr;
}

bool isStockPreviewControl(const QWidget* widget) {
    return widget != nullptr && widget->inherits(kPreviewControlClass);
}

bool isPreviewFileIdSignal(const QMetaMethod& method) {
    return method.methodType() == QMetaMethod::Signal &&
           method.name() == QByteArrayLiteral("signalPreviewFileIdChanged") &&
           method.parameterCount() == 1 && method.parameterType(0) == QMetaType::LongLong;
}

}  // namespace

PlayerTrimInjector::PlayerTrimInjector(QObject* pluginInstance, QObject* parent,
                                       std::function<QString()> currentLibraryRoot)
    : QObject(parent), pluginInstance_(pluginInstance),
      currentLibraryRoot_(std::move(currentLibraryRoot)), scanTimer_(new QTimer(this)) {
    setObjectName(QStringLiteral("BillfishVideoTrimInjector"));
    scanTimer_->setObjectName(QStringLiteral("BillfishVideoTrimScanDebounce"));
    scanTimer_->setSingleShot(true);
    connect(scanTimer_, &QTimer::timeout, this, &PlayerTrimInjector::scanTopLevelWidgets);
}

bool PlayerTrimInjector::isPreviewFileIdSource(QObject* object) {
    if (object == nullptr ||
        QByteArray(object->metaObject()->className()) != QByteArrayLiteral("PreviewDetailsWidget")) {
        return false;
    }
    const QMetaObject* meta = object->metaObject();
    for (int index = 0; index < meta->methodCount(); ++index) {
        if (isPreviewFileIdSignal(meta->method(index))) {
            return true;
        }
    }
    return false;
}

void PlayerTrimInjector::observePreviewFileIdSource(QObject* object) {
    if (!isPreviewFileIdSource(object)) {
        return;
    }
    for (auto iterator = fileIdObservations_.begin(); iterator != fileIdObservations_.end();) {
        if (iterator->source.isNull()) {
            iterator = fileIdObservations_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    auto existing = std::find_if(fileIdObservations_.begin(), fileIdObservations_.end(),
                                 [object](const FileIdObservation& observation) {
                                     return observation.source == object;
                                 });
    const bool known = existing != fileIdObservations_.end();
    if (!known) {
        fileIdObservations_.append({QPointer<QObject>(object), -1});
    }
    const int slotIndex = metaObject()->indexOfSlot(
        "onObservedPreviewFileIdChanged(qint64)");
    if (slotIndex < 0) {
        return;
    }
    const QMetaMethod slot = metaObject()->method(slotIndex);
    const QMetaObject* meta = object->metaObject();
    for (int index = 0; index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (isPreviewFileIdSignal(method)) {
            if (!QObject::connect(object, method, this, slot, Qt::UniqueConnection) && !known) {
                fileIdObservations_.removeLast();
            }
            return;
        }
    }
}

void PlayerTrimInjector::discoverPreviewFileIdSources() {
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (application == nullptr) {
        return;
    }
    for (QWidget* widget : application->allWidgets()) {
        observePreviewFileIdSource(widget);
    }
}

QList<QObject*> PlayerTrimInjector::visiblePreviewFileIdSources() const {
    QList<QObject*> sources;
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (application == nullptr) {
        return sources;
    }
    for (QWidget* widget : application->allWidgets()) {
        if (widget->isVisible() && isPreviewFileIdSource(widget)) {
            sources.append(widget);
        }
    }
    return sources;
}

qint64 PlayerTrimInjector::observedFileId(QObject* source) const {
    for (const FileIdObservation& observation : fileIdObservations_) {
        if (observation.source == source) {
            return observation.lastId;
        }
    }
    return -1;
}

void PlayerTrimInjector::onObservedPreviewFileIdChanged(const qint64 fileId) {
    QObject* source = sender();
    for (FileIdObservation& observation : fileIdObservations_) {
        if (observation.source == source) {
            observation.lastId = fileId > 0 ? fileId : -1;
            return;
        }
    }
}

void PlayerTrimInjector::start() {
    if (started_) {
        return;
    }
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (application == nullptr) {
        return;
    }
    started_ = true;
    application->installEventFilter(this);
    discoverPreviewFileIdSources();
    scheduleScan();
}

bool PlayerTrimInjector::tryInject(QWidget* searchRoot) {
    if (searchRoot == nullptr ||
        propertyIsTrue(searchRoot, TrimEditorDialog::kEditorHostProperty) ||
        searchRoot->objectName() == QString::fromLatin1(TrimEditorDialog::kObjectName)) {
        return false;
    }
    discoverPreviewFileIdSources();
    const QList<QObject*> visibleSources = visiblePreviewFileIdSources();
    QObject* initialSource = visibleSources.size() == 1 ? visibleSources.constFirst() : nullptr;
    if (initialSource == nullptr) {
        QObject* observedSource = nullptr;
        for (const FileIdObservation& observation : fileIdObservations_) {
            if (observation.source != nullptr) {
                if (observedSource != nullptr) {
                    observedSource = nullptr;
                    break;
                }
                observedSource = observation.source;
            }
        }
        initialSource = observedSource;
    }
    const qint64 initialFileId = observedFileId(initialSource);
    bool injected = false;
    const QList<QWidget*> controls = findPreviewControls(searchRoot);
    for (QWidget* previewControl : controls) {
        ImmersionControls::install(previewControl);
        ImmersionPlayback::install(previewControl);
        VideoPlaybackShortcuts::install(previewControl);
        QWidget* playerRoot = findPreviewHost(previewControl);
        if (playerRoot == nullptr) {
            trimDiagnostic(QStringLiteral("path-resolution source=runtime status=scope-unresolved"));
            continue;
        }
        auto* existingResolver = playerRoot->findChild<TrimPanel*>(
            QString::fromLatin1(kResolverObjectName), Qt::FindDirectChildrenOnly);
        auto* existingAction = playerRoot->findChild<QAction*>(
            QString::fromLatin1(kOpenActionObjectName), Qt::FindChildrenRecursively);
        if (existingResolver != nullptr && existingAction != nullptr) {
            playerRoot->setProperty(kInjectedProperty, true);
            searchRoot->setProperty(kInjectedProperty, true);
            (void)existingResolver->refreshRuntimeBindings(
                playerRoot, initialSource, initialFileId, previewControl);
            continue;
        }
        if (propertyIsTrue(playerRoot, kInjectedProperty)) {
            playerRoot->setProperty(kInjectedProperty, false);
        }
        if (existingAction != nullptr) {
            delete existingAction;
        }
        if (existingResolver != nullptr) {
            existingResolver->deleteLater();
        }

        auto* resolver = new TrimPanel(playerRoot, currentLibraryRoot_);
        resolver->setObjectName(QString::fromLatin1(kResolverObjectName));
        resolver->hide();
        (void)resolver->refreshRuntimeBindings(
            playerRoot, initialSource, initialFileId, previewControl);
        if (installOpenEntry(playerRoot, previewControl, resolver) == nullptr) {
            resolver->deleteLater();
            continue;
        }
        playerRoot->setProperty(kInjectedProperty, true);
        searchRoot->setProperty(kInjectedProperty, true);
        injected = true;
    }
    return injected;
}

QWidget* PlayerTrimInjector::findPlayerRoot(QWidget* searchRoot) {
    const QList<QWidget*> controls = findPreviewControls(searchRoot);
    for (QWidget* control : controls) {
        if (QWidget* host = findPreviewHost(control)) {
            return host;
        }
    }
    if (propertyIsTrue(searchRoot, kPlayerRootProperty)) {
        return searchRoot;
    }
    return nullptr;
}

QList<QWidget*> PlayerTrimInjector::findPreviewControls(QWidget* searchRoot) {
    QList<QWidget*> candidates;
    if (searchRoot == nullptr) {
        return candidates;
    }
    QList<QWidget*> widgets{searchRoot};
    widgets.append(searchRoot->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively));
    for (QWidget* widget : widgets) {
        if (isPreviewControlEvidence(widget)) {
            candidates.append(widget);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](QWidget* left, QWidget* right) {
        if (isStockPreviewControl(left) != isStockPreviewControl(right)) {
            return isStockPreviewControl(left);
        }
        int leftDepth = 0;
        for (QWidget* current = left; current != nullptr; current = current->parentWidget()) {
            ++leftDepth;
        }
        int rightDepth = 0;
        for (QWidget* current = right; current != nullptr; current = current->parentWidget()) {
            ++rightDepth;
        }
        return leftDepth > rightDepth;
    });
    QList<QWidget*> result;
    for (QWidget* candidate : candidates) {
        bool coveredByDescendant = false;
        for (QWidget* selected : result) {
            if (candidate != selected && candidate->isAncestorOf(selected)) {
                coveredByDescendant = true;
                break;
            }
            if (candidate != selected && !isStockPreviewControl(candidate) &&
                isStockPreviewControl(selected) && selected->isAncestorOf(candidate)) {
                coveredByDescendant = true;
                break;
            }
        }
        if (!coveredByDescendant) {
            result.append(candidate);
        }
    }
    return result;
}

QWidget* PlayerTrimInjector::findPreviewHost(QWidget* previewControl) {
    if (previewControl == nullptr) {
        return nullptr;
    }
    if (isStockPreviewControl(previewControl)) {
        return previewControl;
    }
    QWidget* candidate = previewControl;
    while (candidate != nullptr) {
        if (propertyIsTrue(candidate, kPlayerRootProperty)) {
            return candidate;
        }
        candidate = candidate->parentWidget();
    }
    return previewControl;
}

bool PlayerTrimInjector::eventFilter(QObject* watched, QEvent* event) {
    if (isTrimPatchObject(watched)) {
        return false;
    }
    switch (event->type()) {
    case QEvent::Show: {
        auto* widget = qobject_cast<QWidget*>(watched);
        observePreviewFileIdSource(widget);
        if (widget != nullptr && (widget->isWindow() || runtimeRelevant(widget))) {
            scheduleScan();
        }
        break;
    }
    case QEvent::ChildPolished: {
        auto* childEvent = static_cast<QChildEvent*>(event);
        observePreviewFileIdSource(childEvent->child());
        scheduleScan();
        break;
    }
    case QEvent::ChildAdded: {
        auto* childEvent = static_cast<QChildEvent*>(event);
        QObject* child = childEvent->child();
        observePreviewFileIdSource(child);
        if (!isTrimPatchObject(child) && (runtimeRelevant(watched) || runtimeRelevant(child))) {
            scheduleScan();
        }
        break;
    }
    case QEvent::DynamicPropertyChange: {
        const QByteArray name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName().toLower();
        if (name.contains("file") || name.contains("path") || name.contains("source") ||
            name.contains("preview") || name.contains("material") || name.contains("billfish.trim")) {
            scheduleScan();
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void PlayerTrimInjector::scanTopLevelWidgets() {
    if (scanInProgress_) {
        return;
    }
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (application == nullptr) {
        return;
    }
    scanInProgress_ = true;
    ++scanPassCount_;
    discoverPreviewFileIdSources();
    const auto topLevels = application->topLevelWidgets();
    for (QWidget* widget : topLevels) {
        (void)tryInject(widget);
    }
    lastScan_.restart();
    scanInProgress_ = false;
    if (rescanRequested_) {
        rescanRequested_ = false;
        scheduleScan();
    }
}

void PlayerTrimInjector::scheduleScan() {
    if (scanInProgress_) {
        rescanRequested_ = true;
        return;
    }
    if (scanTimer_->isActive()) {
        return;
    }
    int delayMs = kScanDebounceMs;
    if (lastScan_.isValid()) {
        const qint64 remainingMs = kMinimumScanIntervalMs - lastScan_.elapsed();
        if (remainingMs > delayMs) {
            delayMs = static_cast<int>(remainingMs);
        }
    }
    scanTimer_->start(delayMs);
}

QAction* PlayerTrimInjector::installOpenEntry(QWidget* playerRoot, QWidget* previewControl,
                                              TrimPanel* resolver) {
    if (playerRoot == nullptr || previewControl == nullptr || resolver == nullptr) {
        return nullptr;
    }

    const auto configureAction = [this, playerRoot, previewControl, resolver](QAction* action) {
        action->setObjectName(QString::fromLatin1(kOpenActionObjectName));
        action->setText(QStringLiteral("剪辑视频..."));
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
        action->setShortcutContext(Qt::WindowShortcut);
        playerRoot->addAction(action);
        const QPointer<QWidget> guardedRoot(playerRoot);
        const QPointer<QWidget> guardedControl(previewControl);
        const QPointer<TrimPanel> guardedResolver(resolver);
        connect(action, &QAction::triggered, this,
                [this, guardedRoot, guardedControl, guardedResolver] {
            if (guardedRoot != nullptr && guardedControl != nullptr && guardedResolver != nullptr) {
                openEditor(guardedRoot, guardedControl, guardedResolver);
            }
        });
    };

    const QStringList menuButtonNames{
        QString::fromLatin1(kFrameButtonObjectName),
        QStringLiteral("videoFrameBtn"),
    };
    for (const QString& objectName : menuButtonNames) {
        auto* frameButton = previewControl->findChild<QAbstractButton*>(
            objectName, Qt::FindChildrenRecursively);
        if (frameButton == nullptr && previewControl->objectName() == objectName) {
            frameButton = qobject_cast<QAbstractButton*>(previewControl);
        }
        QMenu* frameMenu = nullptr;
        if (auto* toolButton = qobject_cast<QToolButton*>(frameButton)) {
            frameMenu = toolButton->menu();
        } else if (auto* pushButton = qobject_cast<QPushButton*>(frameButton)) {
            frameMenu = pushButton->menu();
        }
        if (frameMenu == nullptr) {
            continue;
        }
        if (!frameMenu->actions().isEmpty()) {
            frameMenu->addSeparator();
        }
        QAction* action = frameMenu->addAction(QStringLiteral("剪辑视频..."));
        configureAction(action);
        return action;
    }

    auto* titleFrameButton = previewControl->findChild<QAbstractButton*>(
        QStringLiteral("videoFrameBtn"), Qt::FindChildrenRecursively);
    QWidget* titleBar = titleFrameButton != nullptr ? titleFrameButton->parentWidget() : nullptr;
    auto* titleLayout = titleBar != nullptr ? qobject_cast<QBoxLayout*>(titleBar->layout()) : nullptr;
    if (titleFrameButton == nullptr || titleLayout == nullptr || !isHorizontal(titleLayout)) {
        return nullptr;
    }
    const int frameIndex = titleLayout->indexOf(titleFrameButton);
    if (frameIndex < 0) {
        return nullptr;
    }

    auto* action = new QAction(titleBar);
    configureAction(action);
    auto* openButton = new QToolButton(titleBar);
    openButton->setObjectName(QString::fromLatin1(kOpenButtonObjectName));
    openButton->setDefaultAction(action);
    openButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    openButton->setAutoRaise(true);
    openButton->setToolTip(QStringLiteral("剪辑当前视频 (Ctrl+E)"));
    const bool dark = titleBar->palette().color(QPalette::Window).lightness() < 128;
    openButton->setStyleSheet(QStringLiteral(
        "QToolButton{border:0;padding:4px;color:%1;background:transparent;}"
        "QToolButton:hover{background:%2;border-radius:6px;}")
        .arg(dark ? QStringLiteral("#D9D9D9") : QStringLiteral("#525252"),
             dark ? QStringLiteral("#4D626262") : QStringLiteral("#40CCCCCC")));
    titleLayout->insertWidget(frameIndex, openButton);
    return action;
}

void PlayerTrimInjector::openEditor(QWidget* playerRoot, QWidget* previewControl,
                                    TrimPanel* resolver) {
    if (playerRoot == nullptr || previewControl == nullptr || resolver == nullptr) {
        return;
    }
    const auto reportOpenFailure = [playerRoot, resolver](const QString& fallback) {
        const QString reason = resolver->statusText().trimmed().isEmpty()
            ? fallback
            : resolver->statusText().trimmed();
        if (billfish::automation::AppInputScope::active()) {
            auto* warning = new QMessageBox(QMessageBox::Warning, QStringLiteral("剪辑视频"),
                                            reason, QMessageBox::Ok, playerRoot);
            warning->setObjectName(QStringLiteral("BillfishVideoTrimSourceError"));
            warning->setAttribute(Qt::WA_ShowWithoutActivating, true);
            warning->setAttribute(Qt::WA_DeleteOnClose, true);
            warning->setWindowModality(Qt::NonModal);
            warning->show();
            return;
        }
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            trimDiagnostic(QStringLiteral("path-resolution source=open status=offscreen-rejected reason=%1")
                               .arg(reason));
            return;
        }
        QMessageBox::warning(playerRoot, QStringLiteral("剪辑视频"), reason);
    };
    discoverPreviewFileIdSources();
    const QList<QObject*> visibleSources = visiblePreviewFileIdSources();
    if (visibleSources.size() != 1) {
        (void)resolver->refreshRuntimeBindings(playerRoot, nullptr, -1, previewControl, true);
        reportOpenFailure(visibleSources.isEmpty()
                              ? QStringLiteral("未找到当前可见视频的唯一标识源，已拒绝剪辑。")
                              : QStringLiteral("检测到多个可见视频标识源，已拒绝剪辑。"));
        return;
    }
    QObject* selectedSource = visibleSources.constFirst();
    const qint64 selectedFileId = observedFileId(selectedSource);
    if (!resolver->refreshRuntimeBindings(playerRoot, selectedSource, selectedFileId,
                                          previewControl, true)) {
        trimDiagnostic(QStringLiteral("path-resolution source=open status=rejected"));
        reportOpenFailure(QStringLiteral("无法确认当前正在播放的视频，已拒绝剪辑。"));
        return;
    }
    const QString sourcePath = resolver->sourcePath();
    const qint64 durationMs = resolver->mediaDurationMs();
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
        trimDiagnostic(QStringLiteral("path-resolution source=open status=missing-source"));
        reportOpenFailure(QStringLiteral("当前视频文件不可用，已拒绝剪辑。"));
        return;
    }
    const qint64 initialPositionMs = resolver->currentPlaybackPositionMs();
    if (editorDialog_ != nullptr) {
        // Every invocation is a fresh snapshot of the active Billfish preview.
        // If an export is still running, closeEvent starts cancellation and
        // keeps that editor alive until the worker has stopped safely.
        QPointer<TrimEditorDialog> previousEditor = editorDialog_;
        if (!previousEditor->close()) {
            const bool appInput = billfish::automation::AppInputScope::active();
            previousEditor->setAttribute(Qt::WA_ShowWithoutActivating, appInput);
            previousEditor->show();
            previousEditor->showNormal();
            if (!appInput) {
                previousEditor->raise();
                previousEditor->activateWindow();
            }
            return;
        }
        if (previousEditor != nullptr) {
            delete previousEditor.data();
        }
        editorDialog_.clear();
    }

    auto* dialog = new TrimEditorDialog(playerRoot);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    const bool appInput = billfish::automation::AppInputScope::active();
    dialog->setAttribute(Qt::WA_ShowWithoutActivating, appInput);
    editorDialog_ = dialog;
    editorDialog_->setSource(sourcePath, durationMs, initialPositionMs);
    editorDialog_->show();
    editorDialog_->showNormal();
    if (!appInput) {
        editorDialog_->raise();
        editorDialog_->activateWindow();
    }
}

void installPlayerTrimInjection(QObject* pluginInstance,
                                std::function<QString()> currentLibraryRoot) {
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (application == nullptr) {
        return;
    }
    const auto install = [application, pluginInstance, currentLibraryRoot] {
        if (application->findChild<PlayerTrimInjector*>(QStringLiteral("BillfishVideoTrimInjector"),
                                                        Qt::FindDirectChildrenOnly) != nullptr) {
            return;
        }
        auto* injector = new PlayerTrimInjector(pluginInstance, application, currentLibraryRoot);
        injector->start();
    };
    if (QThread::currentThread() == application->thread()) {
        install();
    } else {
        QMetaObject::invokeMethod(application, install, Qt::QueuedConnection);
    }
}

}  // namespace billfish::trim
