#include "ui/trim_panel.hpp"
#include "automation/app_input_scope.hpp"
#include "automation/scoped_popup.hpp"

#include <utility>

#include "ui/timeline_trim_overlay.hpp"
#include "ui/trim_diagnostics.hpp"
#include "ui/trim_export_worker.hpp"

#include <QAbstractButton>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMetaMethod>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPalette>
#include <QProgressBar>
#include <QSet>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QThread>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace billfish::trim {

namespace {
constexpr auto kTimelineObjectName = "PreviewControlWidget_m_pVideoSlider";
constexpr auto kPlayButtonObjectName = "PreviewControlWidget_StartBnt";
constexpr auto kPauseButtonObjectName = "PreviewControlWidget_PauseBnt";
constexpr auto kInputPathProperty = "billfish.trim.inputPath";
constexpr auto kFileIdSignalName = "signalPreviewFileIdChanged";
constexpr auto kVideoFramesSignalName = "videoFramesChanged";
constexpr auto kVideoTotalFramesSignalName = "videoTotalFramesChanged";

class TrimExportButton final : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (billfish::automation::AppInputScope::active() && event->button() == Qt::LeftButton && menu()) {
            QStyleOptionToolButton option;
            initStyleOption(&option);
            const QRect menuRect = style()->subControlRect(
                QStyle::CC_ToolButton, &option, QStyle::SC_ToolButtonMenu, this);
            if (menuRect.isValid() && menuRect.contains(event->pos())) {
                appMenuPress_ = true;
                billfish::automation::showScopedPopup(menu(), mapToGlobal(rect().bottomLeft()));
                event->accept();
                return;
            }
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (appMenuPress_ && event->button() == Qt::LeftButton) {
            appMenuPress_ = false;
            event->accept();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }

private:
    bool appMenuPress_ = false;
};

QString stageText(const TrimStage stage) {
    switch (stage) {
    case TrimStage::Preparing:
        return QStringLiteral("准备导出");
    case TrimStage::ProbingInput:
        return QStringLiteral("分析源视频");
    case TrimStage::Trimming:
        return QStringLiteral("正在生成片段");
    case TrimStage::ProbingOutput:
        return QStringLiteral("检查输出");
    case TrimStage::Validating:
        return QStringLiteral("验证片段");
    case TrimStage::Committing:
        return QStringLiteral("提交输出");
    case TrimStage::Completed:
        return QStringLiteral("已完成");
    }
    return QStringLiteral("处理中");
}

QString valueAsPath(const QVariant& value) {
    if (value.userType() == QMetaType::QUrl) {
        return value.toUrl().toLocalFile();
    }
    return value.toString().trimmed();
}

QString objectAddress(const QObject* object) {
    if (object == nullptr) {
        return QStringLiteral("null");
    }
    return QStringLiteral("0x%1")
        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(object)),
             static_cast<int>(sizeof(void*) * 2), 16, QLatin1Char('0'));
}

QString objectIdentity(const QObject* object) {
    if (object == nullptr) {
        return QStringLiteral("null");
    }
    return QStringLiteral("%1@%2")
        .arg(QString::fromLatin1(object->metaObject()->className()), objectAddress(object));
}

bool isDescendantOf(const QObject* object, const QObject* ancestor) {
    if (object == nullptr || ancestor == nullptr) {
        return false;
    }
    const QObject* current = object;
    while (current != nullptr) {
        if (current == ancestor) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

QString parentChain(const QObject* object) {
    QStringList parents;
    const QObject* current = object != nullptr ? object->parent() : nullptr;
    for (int depth = 0; current != nullptr && depth < 4; ++depth, current = current->parent()) {
        parents.append(objectIdentity(current));
    }
    return parents.isEmpty() ? QStringLiteral("none") : parents.join(QStringLiteral("<"));
}

QString compactTimestamp(const qint64 milliseconds) {
    const quint64 magnitude = milliseconds <= 0 ? 0U : static_cast<quint64>(milliseconds);
    const quint64 hours = magnitude / 3'600'000U;
    const quint64 minutes = (magnitude / 60'000U) % 60U;
    const quint64 seconds = (magnitude / 1'000U) % 60U;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool usesDarkPalette(const QWidget* widget) {
    const QColor windowColor = widget != nullptr
        ? widget->palette().color(QPalette::Window)
        : QApplication::palette().color(QPalette::Window);
    return windowColor.lightness() < 128;
}

QString billfishResourcePath(const QString& relativePath) {
    QDir directory(QCoreApplication::applicationDirPath());
    for (int level = 0; level < 5; ++level) {
        const QString candidate = directory.filePath(relativePath);
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    const QString workingDirectoryCandidate = QDir::current().filePath(relativePath);
    return QFileInfo::exists(workingDirectoryCandidate)
        ? QFileInfo(workingDirectoryCandidate).absoluteFilePath()
        : QString{};
}

QString primaryToolButtonStyle(const bool dark) {
    const QString skin = dark ? QStringLiteral("black") : QStringLiteral("white");
    const QString path = billfishResourcePath(
        QStringLiteral("Resources/skin/%1/ok_button.css").arg(skin));
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString style = QString::fromUtf8(file.readAll());
        style.replace(QStringLiteral("QPushButton"), QStringLiteral("QToolButton"));
        return style;
    }
    return QStringLiteral(
        "QToolButton{color:%1;font-size:13px;background-color:#4F7DFF;border:none;"
        "border-radius:4px;padding:0 12px;}"
        "QToolButton:hover{color:%1;background-color:#5B86FF;}"
        "QToolButton:disabled{color:%2;background-color:#3A5299;}")
        .arg(dark ? QStringLiteral("#FAFAFA") : QStringLiteral("#FCFCFC"),
             dark ? QStringLiteral("#8D8D8D") : QStringLiteral("#D9D9D9"));
}
}  // namespace

TrimPanel::TrimPanel(QWidget* parent, std::function<QString()> currentLibraryRoot)
    : QWidget(parent), currentLibraryRoot_(std::move(currentLibraryRoot)) {
    setObjectName(QString::fromLatin1(kObjectName));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);
    setMinimumHeight(58);
    setMaximumHeight(60);
    setAutoFillBackground(true);

    sourceLabel_ = new QLabel(QStringLiteral("等待当前视频"), this);
    sourceLabel_->setObjectName(QStringLiteral("BillfishVideoTrimSourceLabel"));
    sourceLabel_->hide();
    inLabel_ = new QLabel(this);
    inLabel_->setObjectName(QStringLiteral("BillfishVideoTrimTimeLabel"));
    inLabel_->setMinimumWidth(170);
    outLabel_ = new QLabel(this);
    outLabel_->setObjectName(QStringLiteral("BillfishVideoTrimOutLabel"));
    outLabel_->hide();
    durationLabel_ = new QLabel(this);
    durationLabel_->setObjectName(QStringLiteral("BillfishVideoTrimDurationLabel"));
    durationLabel_->hide();

    const auto makeToolButton = [this](const QString& text, const QString& objectName) {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setObjectName(objectName);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAutoRaise(true);
        return button;
    };
    setInButton_ = makeToolButton(QStringLiteral("设 In"), QStringLiteral("BillfishVideoTrimSetInButton"));
    setOutButton_ = makeToolButton(QStringLiteral("设 Out"), QStringLiteral("BillfishVideoTrimSetOutButton"));
    setInButton_->hide();
    setOutButton_->hide();
    previewButton_ = makeToolButton(QString{}, QStringLiteral("BillfishVideoTrimPreviewButton"));
    previewButton_->setCheckable(true);
    previewButton_->setFixedSize(40, 40);
    exportButton_ = new TrimExportButton(this);
    exportButton_->setText(QStringLiteral("导出片段"));
    exportButton_->setObjectName(QStringLiteral("BillfishVideoTrimExportButton"));
    exportButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    exportButton_->setAutoRaise(false);
    exportButton_->setPopupMode(QToolButton::MenuButtonPopup);
    exportButton_->setFixedSize(104, 42);
    copyButton_ = makeToolButton(QStringLiteral("复制到剪贴板"), QStringLiteral("BillfishVideoTrimCopyButton"));
    copyButton_->setAutoRaise(false);
    copyButton_->setFixedSize(142, 42);
    cancelButton_ = makeToolButton(QStringLiteral("取消"), QStringLiteral("BillfishVideoTrimCancelButton"));
    cancelButton_->setAutoRaise(false);
    cancelButton_->setFixedSize(72, 42);
    cancelButton_->hide();
    modeButton_ = makeToolButton(QStringLiteral("精确"), QStringLiteral("BillfishVideoTrimMode"));
    modeButton_->hide();
    modeButton_->setToolTip(QStringLiteral("精确: 重编码到准确帧; 快速: 关键帧附近无损复制"));
    auto* modeMenu = new QMenu(exportButton_);
    modeMenu->setObjectName(QStringLiteral("BillfishVideoTrimExportModeMenu"));
    auto* modeGroup = new QActionGroup(modeMenu);
    modeGroup->setExclusive(true);
    auto* accurateAction = modeMenu->addAction(QStringLiteral("精确导出（帧准确）"));
    accurateAction->setCheckable(true);
    accurateAction->setChecked(true);
    accurateAction->setData(static_cast<int>(TrimMode::AccurateReencode));
    modeGroup->addAction(accurateAction);
    auto* fastAction = modeMenu->addAction(QStringLiteral("快速导出（关键帧）"));
    fastAction->setCheckable(true);
    fastAction->setData(static_cast<int>(TrimMode::FastCopy));
    modeGroup->addAction(fastAction);
    exportButton_->setMenu(modeMenu);
    connect(modeMenu, &QMenu::triggered, this, [this](QAction* action) {
        setTrimMode(static_cast<TrimMode>(action->data().toInt()));
    });

    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName(QStringLiteral("BillfishVideoTrimProgress"));
    progressBar_->setTextVisible(false);
    progressBar_->setRange(0, 100);
    progressBar_->setMaximumHeight(3);
    progressBar_->hide();
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("BillfishVideoTrimStatus"));
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLabel_->setMinimumWidth(0);
    statusLabel_->setMaximumWidth(260);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLabel_->hide();

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(progressBar_);
    auto* actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(14, 7, 14, 7);
    actionRow->setSpacing(14);
    actionRow->addWidget(previewButton_);
    actionRow->addWidget(inLabel_);
    actionRow->addWidget(statusLabel_, 1);
    actionRow->addStretch(1);
    actionRow->addWidget(exportButton_);
    actionRow->addWidget(copyButton_);
    actionRow->addWidget(cancelButton_);
    outerLayout->addLayout(actionRow, 1);

    connect(setInButton_, &QToolButton::clicked, this, &TrimPanel::setInPointFromPlayhead);
    connect(setOutButton_, &QToolButton::clicked, this, &TrimPanel::setOutPointFromPlayhead);
    connect(previewButton_, &QToolButton::toggled, this, &TrimPanel::togglePreview);
    connect(exportButton_, &QToolButton::clicked, this, &TrimPanel::chooseExportPath);
    connect(copyButton_, &QToolButton::clicked, this, [this] { (void)copySelectionToClipboard(); });
    connect(cancelButton_, &QToolButton::clicked, this, &TrimPanel::cancelExport);
    applyCompactStyle();
    updateLabels();
    updatePreviewButtonVisual();
    updateActionState();
}

TrimPanel::~TrimPanel() {
    if (worker_ != nullptr) {
        worker_->requestCancel();
    }
    delete overlay_;
}

TimelineTrimOverlay* TrimPanel::timelineOverlay() const noexcept { return overlay_; }

RangeSelection TrimPanel::selection() const noexcept {
    if (overlay_ == nullptr) {
        return {};
    }
    return {timelineValueToMs(overlay_->inValue()), timelineValueToMs(overlay_->outValue())};
}

TrimMode TrimPanel::trimMode() const noexcept {
    return selectedMode_;
}

void TrimPanel::setTrimMode(const TrimMode mode) {
    selectedMode_ = mode;
    if (exportButton_->menu() != nullptr) {
        for (QAction* action : exportButton_->menu()->actions()) {
            if (action->data().isValid()) {
                action->setChecked(action->data().toInt() == static_cast<int>(mode));
            }
        }
    }
    modeButton_->setText(mode == TrimMode::AccurateReencode
                             ? QStringLiteral("精确")
                             : QStringLiteral("快速"));
    exportButton_->setToolTip(mode == TrimMode::AccurateReencode
                                  ? QStringLiteral("当前为精确导出；使用箭头切换模式")
                                  : QStringLiteral("当前为快速导出；使用箭头切换模式"));
}

bool TrimPanel::isPreviewing() const noexcept { return previewing_; }
bool TrimPanel::isBusy() const noexcept { return busy_; }
QSlider* TrimPanel::hostTimeline() const noexcept { return hostTimeline_; }
QString TrimPanel::sourcePath() const { return sourcePath_; }
qint64 TrimPanel::mediaDurationMs() const noexcept { return mediaDurationMs_; }
qint64 TrimPanel::currentPlaybackPositionMs() const noexcept {
    if (mediaDurationMs_ <= 0) {
        return 0;
    }
    if (currentVideoFrame_ >= 0 && totalVideoFrames_ > 0) {
        const double fraction = static_cast<double>(currentVideoFrame_) /
                                static_cast<double>(totalVideoFrames_);
        return static_cast<qint64>(std::llround(
            std::clamp(fraction, 0.0, 1.0) * static_cast<double>(mediaDurationMs_)));
    }
    return hostTimeline_ != nullptr ? timelineValueToMs(hostTimeline_->value()) : 0;
}
QString TrimPanel::statusText() const { return statusLabel_->text(); }
QSize TrimPanel::sizeHint() const { return QSize(720, 60); }
QSize TrimPanel::minimumSizeHint() const { return QSize(0, 58); }

void TrimPanel::applyVerifiedSource(const QString& path, const qint64 durationMs) {
    const QFileInfo info(path);
    if (!info.isFile() || durationMs <= 0) {
        return;
    }
    const QString absolutePath = info.absoluteFilePath();
    if (QString::compare(sourcePath_, absolutePath, Qt::CaseInsensitive) == 0) {
        if (mediaDurationMs_ != durationMs) {
            mediaDurationMs_ = durationMs;
            updateLabels();
            updateActionState();
        }
        return;
    }
    trimDiagnostic(QStringLiteral("path-resolution source=shared status=applied path=%1 durationMs=%2 prior=%3")
                       .arg(absolutePath)
                       .arg(durationMs)
                       .arg(sourcePath_));
    setResolvedSource(absolutePath, durationMs);
}

bool TrimPanel::attachToPlayer(QWidget* playerRoot, QObject* selectedFileIdSource,
                               const qint64 observedFileId) {
    if (playerRoot == nullptr) {
        return false;
    }
    QSlider* timeline = findHostTimeline(playerRoot);
    if (timeline == nullptr) {
        return false;
    }
    if (hostTimeline_ != nullptr) {
        disconnect(hostTimeline_, nullptr, this, nullptr);
    }
    delete overlay_;
    playerRoot_ = playerRoot;
    selectedFileIdSource_ = selectedFileIdSource;
    selectedObservedFileId_ = observedFileId;
    hostTimeline_ = timeline;
    hostPlayButton_ = findButton(playerRoot, QString::fromLatin1(kPlayButtonObjectName));
    hostPauseButton_ = findButton(playerRoot, QString::fromLatin1(kPauseButtonObjectName));
    overlay_ = new TimelineTrimOverlay(timeline);
    connect(overlay_, &TimelineTrimOverlay::selectionChanged,
            this, &TrimPanel::onOverlaySelectionChanged);
    connect(timeline, &QSlider::valueChanged, this, &TrimPanel::onTimelineValueChanged);
    connect(timeline, &QObject::destroyed, this, [this] {
        stopPreview();
        hostTimeline_.clear();
        overlay_.clear();
        updateActionState();
    });
    (void)refreshRuntimeBindings(playerRoot, selectedFileIdSource, observedFileId);
    updateLabels();
    updateActionState();
    return true;
}

bool TrimPanel::refreshRuntimeBindings(QWidget* playerRoot, QObject* selectedFileIdSource,
                                       const qint64 observedFileId, QWidget* previewControl,
                                       const bool diagnoseBindingFailure) {
    if (playerRoot == nullptr) {
        return false;
    }
    const bool scopeChanged = playerRoot_ != nullptr && playerRoot_ != playerRoot;
    playerRoot_ = playerRoot;
    if (hostTimeline_ == nullptr) {
        hostTimeline_ = findHostTimeline(playerRoot);
        hostPlayButton_ = findButton(playerRoot, QString::fromLatin1(kPlayButtonObjectName));
        hostPauseButton_ = findButton(playerRoot, QString::fromLatin1(kPauseButtonObjectName));
    }
    if (previewControl != nullptr) {
        previewControl_ = previewControl;
    }
    diagnoseBindingFailure_ = diagnoseBindingFailure;
    const auto finish = [this](const bool result) {
        diagnoseBindingFailure_ = false;
        return result;
    };
    selectedFileIdSource_ = selectedFileIdSource;
    selectedObservedFileId_ = observedFileId;
    if (scopeChanged && !explicitInputOverride_) {
        invalidateResolvedSource(QStringLiteral("当前播放器作用域已变化，无法确认当前视频"));
    }
    connectFileIdSource(playerRoot);
    connectPlaybackFrameSources(playerRoot);
    const QString refreshSignature = QStringLiteral("%1|%2|%3|%4")
                                         .arg(fileIdEmitter_ != nullptr ? 1 : 0)
                                         .arg(sourcePath_)
                                         .arg(currentFileId_)
                                         .arg(lastObservedFileId_);
    if (trimDiagnosticsEnabled() && refreshSignature != diagnosticRefreshSignature_) {
        diagnosticRefreshSignature_ = refreshSignature;
        trimDiagnostic(QStringLiteral("path-resolution source=runtime status=bindings emitter=%1 sourcePath=%2 fileId=%3 last=%4")
                           .arg(fileIdEmitter_ != nullptr ? 1 : 0)
                           .arg(sourcePath_)
                           .arg(currentFileId_)
                           .arg(lastObservedFileId_));
    }
    if (explicitInputOverride_) {
        return finish(!sourcePath_.isEmpty() && QFileInfo(sourcePath_).isFile());
    }
    if (sourcePath_.isEmpty()) {
        QWidget* propertyRoot = playerRoot;
        while (propertyRoot != nullptr &&
               !propertyRoot->property(kInputPathProperty).isValid()) {
            propertyRoot = propertyRoot->parentWidget();
        }
        const QString explicitPath = propertyRoot == nullptr
            ? QString{}
            : valueAsPath(propertyRoot->property(kInputPathProperty));
        if (!explicitPath.isEmpty() && QFileInfo(explicitPath).isFile()) {
            explicitInputOverride_ = true;
            const qint64 durationMs = propertyRoot->property("billfish.trim.durationMs").toLongLong();
            setResolvedSource(explicitPath, durationMs);
            return finish(true);
        }
    }
    return finish(synchronizeCurrentSource());
}

bool TrimPanel::exportToPath(const QString& outputPath) {
    if (busy_) {
        setStatus(QStringLiteral("已有片段任务正在运行"), true);
        return false;
    }
    QString normalizedOutput = outputPath.trimmed();
    if (normalizedOutput.isEmpty()) {
        return false;
    }
    if (QFileInfo(normalizedOutput).suffix().isEmpty()) {
        QString extension = QFileInfo(sourcePath_).suffix();
        if (extension.isEmpty()) {
            extension = QStringLiteral("mp4");
        }
        normalizedOutput += QStringLiteral(".") + extension;
    }
    const QString absoluteOutput = QFileInfo(normalizedOutput).absoluteFilePath();
    if (absoluteOutput.isEmpty()) {
        return false;
    }
    return startTrim(makeRequest(absoluteOutput), JobPurpose::Export);
}

bool TrimPanel::copySelectionToClipboard() {
    if (busy_) {
        setStatus(QStringLiteral("已有片段任务正在运行"), true);
        return false;
    }
    TrimRequest request = makeRequest({});
    ClipCache cache(cacheRoot());
    request.outputPath = cache.outputPathFor(request);
    const QString validationError = validateTrimRequest(request, mediaDurationMs_);
    if (!validationError.isEmpty()) {
        setStatus(validationError, true);
        return false;
    }
    const ClipCacheLookup lookup = cache.lookup(request);
    if (!lookup.error.isEmpty()) {
        setStatus(QStringLiteral("读取片段缓存失败: %1").arg(lookup.error), true);
        return false;
    }
    if (lookup.hit) {
        return copyFileToClipboard(lookup.entry.outputPath);
    }
    request.overwrite = true;
    return startTrim(std::move(request), JobPurpose::Copy);
}

void TrimPanel::stopPreview() {
    if (previewButton_->isChecked()) {
        previewButton_->setChecked(false);
        return;
    }
    if (previewing_) {
        previewing_ = false;
        updatePreviewButtonVisual();
        emit previewStopped();
    }
}

void TrimPanel::cancelExport() {
    if (!busy_ || worker_ == nullptr) {
        return;
    }
    worker_->requestCancel();
    cancelButton_->setEnabled(false);
    setStatus(QStringLiteral("正在取消..."));
    progressBar_->setRange(0, 0);
}

void TrimPanel::setInPointFromPlayhead() {
    if (hostTimeline_ != nullptr && overlay_ != nullptr) {
        overlay_->setInValue(hostTimeline_->value());
    }
}

void TrimPanel::setOutPointFromPlayhead() {
    if (hostTimeline_ != nullptr && overlay_ != nullptr) {
        overlay_->setOutValue(hostTimeline_->value());
    }
}

void TrimPanel::togglePreview(const bool enabled) {
    if (enabled) {
        const RangeSelection range = selection();
        if (hostTimeline_ == nullptr || overlay_ == nullptr || !range.isValid()) {
            previewButton_->setChecked(false);
            return;
        }
        previewing_ = true;
        updatePreviewButtonVisual();
        // Let the independent editor establish its A/B loop before the seek
        // and play commands are forwarded to the host controls.
        emit previewStarted(range.startMs, range.endMs);
        if (!previewing_) {
            return;
        }
        seekHostTimeline(overlay_->inValue());
        clickHostButton(hostPlayButton_);
        return;
    }
    if (!previewing_) {
        return;
    }
    previewing_ = false;
    updatePreviewButtonVisual();
    clickHostButton(hostPauseButton_);
    emit previewStopped();
}

void TrimPanel::chooseExportPath() {
    const QString suggested = defaultExportPath();
    const QString extension = QFileInfo(suggested).suffix();
    const QString filter = extension.isEmpty()
        ? QStringLiteral("视频文件 (*)")
        : QStringLiteral("视频文件 (*.%1);;所有文件 (*)").arg(extension);
    if (billfish::automation::AppInputScope::active()) {
        auto* dialog = new QFileDialog(this);
        dialog->setOption(QFileDialog::DontUseNativeDialog, true);
        // QFileDialog's built-in overwrite prompt uses a nested modal loop.
        // Keep the same decision visible through an asynchronous Qt message box.
        dialog->setOption(QFileDialog::DontConfirmOverwrite, true);
        dialog->setObjectName(QStringLiteral("BillfishVideoTrimExportDialog"));
        dialog->setWindowTitle(QStringLiteral("导出片段"));
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setFileMode(QFileDialog::AnyFile);
        dialog->setNameFilter(filter);
        dialog->setDefaultSuffix(extension);
        dialog->setDirectory(QFileInfo(suggested).absolutePath());
        dialog->selectFile(QFileInfo(suggested).fileName());
        dialog->setWindowModality(Qt::NonModal);
        dialog->setAttribute(Qt::WA_ShowWithoutActivating, true);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        connect(dialog, &QFileDialog::fileSelected, this, [this, extension](QString selected) {
            if (selected.isEmpty()) return;
            if (QFileInfo(selected).suffix().isEmpty() && !extension.isEmpty()) {
                selected += QStringLiteral(".") + extension;
            }
            if (QFileInfo::exists(selected)) {
                auto* confirm = new QMessageBox(QMessageBox::Question, QStringLiteral("确认另存为"),
                    QStringLiteral("文件 %1 已存在. 是否替换?").arg(QFileInfo(selected).fileName()),
                    QMessageBox::Yes | QMessageBox::No, this);
                confirm->setObjectName(QStringLiteral("BillfishVideoTrimOverwriteConfirm"));
                confirm->setDefaultButton(QMessageBox::No);
                confirm->setWindowModality(Qt::NonModal);
                confirm->setAttribute(Qt::WA_ShowWithoutActivating, true);
                confirm->setAttribute(Qt::WA_DeleteOnClose, true);
                connect(confirm, &QMessageBox::finished, this, [this, selected](int result) {
                    if (result == QMessageBox::Yes) (void)exportToPath(selected);
                });
                confirm->show();
            } else {
                (void)exportToPath(selected);
            }
        });
        dialog->show();
        return;
    }
    QString selected = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出片段"), suggested, filter);
    if (!selected.isEmpty() && QFileInfo(selected).suffix().isEmpty() && !extension.isEmpty()) {
        selected += QStringLiteral(".") + extension;
    }
    if (!selected.isEmpty()) {
        (void)exportToPath(selected);
    }
}

void TrimPanel::onTimelineValueChanged(const int value) {
    updateLabels();
    if (!previewing_ || wrappingPreview_ || overlay_ == nullptr) {
        return;
    }
    if (value < overlay_->outValue()) {
        return;
    }
    wrappingPreview_ = true;
    seekHostTimeline(overlay_->inValue());
    clickHostButton(hostPlayButton_);
    wrappingPreview_ = false;
}

void TrimPanel::onOverlaySelectionChanged(const int inValue, const int outValue) {
    updateLabels();
    const RangeSelection range = selection();
    if (previewing_ && !range.isValid()) {
        stopPreview();
    }
    emit selectionChanged(range.startMs, range.endMs);
    if (hostTimeline_ != nullptr &&
        (hostTimeline_->value() < inValue || hostTimeline_->value() > outValue)) {
        // A handle can move past the playhead while playback is paused. Keep
        // the player and the displayed selection coherent immediately.
        seekHostTimeline(inValue);
    }
    updateActionState();
}

void TrimPanel::onPreviewFileIdChanged(const qint64 fileId) {
    if (explicitInputOverride_ || sender() != fileIdEmitter_) {
        return;
    }
    if (fileId <= 0) {
        lastObservedFileId_ = -1;
        invalidateResolvedSource(QStringLiteral("当前视频标识无效，已拒绝剪辑"));
        return;
    }
    if (lastObservedFileId_ != fileId) {
        currentVideoFrame_ = -1;
        totalVideoFrames_ = -1;
    }
    lastObservedFileId_ = fileId;
    (void)resolveFileId(fileId);
}

void TrimPanel::onVideoFramesChanged(const qint64 secondFrames,
                                     const qint64 currentFrames) {
    Q_UNUSED(secondFrames)
    if (playbackFrameSources_.contains(sender()) && currentFrames >= 0) {
        currentVideoFrame_ = currentFrames;
    }
}

void TrimPanel::onVideoTotalFramesChanged(const qint64 totalFrames,
                                           const qint64 lastSecondFrames) {
    Q_UNUSED(lastSecondFrames)
    if (playbackFrameSources_.contains(sender()) && totalFrames > 0) {
        totalVideoFrames_ = totalFrames;
    }
}

void TrimPanel::onWorkerProgress(const int stage, const qint64 processedMs,
                                 const qint64 totalMs, const int percent) {
    Q_UNUSED(processedMs)
    Q_UNUSED(totalMs)
    const TrimStage trimStage = static_cast<TrimStage>(stage);
    const QString text = stageText(trimStage);
    if (trimStage == TrimStage::Trimming && percent >= 0) {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(std::clamp(percent, 0, 100));
        progressBar_->setFormat(QStringLiteral("%1  %2%").arg(text).arg(std::clamp(percent, 0, 100)));
    } else {
        progressBar_->setRange(0, 0);
        progressBar_->setFormat(text);
    }
    setStatus(text);
    emit operationProgress(std::clamp(percent, 0, 100), text);
}

void TrimPanel::onWorkerCompleted(const bool success, const QString& outputPath,
                                  const QString& error, const qint64 elapsedMs) {
    Q_UNUSED(elapsedMs)
    const JobPurpose purpose = activePurpose_;
    const TrimRequest request = activeRequest_;
    worker_.clear();
    workerThread_.clear();
    activePurpose_ = JobPurpose::None;
    setBusy(false);
    if (!success) {
        const QString message = error.isEmpty() ? QStringLiteral("片段任务失败") : error;
        setStatus(message, true);
        emit operationFailed(message);
        return;
    }

    if (purpose == JobPurpose::Copy) {
        ClipCache cache(cacheRoot());
        QString cacheError;
        const bool cacheRecorded = cache.record(request, outputPath, &cacheError);
        if (!copyFileToClipboard(outputPath)) {
            return;
        }
        const ClipCachePruneResult prune = cache.prune(
            1024LL * 1024LL * 1024LL, 64, QSet<QString>{cache.keyFor(request)});
        if (!cacheRecorded) {
            setStatus(QStringLiteral("片段已复制；缓存记录失败，下次将重新生成: %1").arg(cacheError));
        } else if (!prune.success) {
            setStatus(QStringLiteral("片段已复制；缓存清理失败: %1").arg(prune.error));
        } else if (!prune.limitsSatisfied) {
            setStatus(QStringLiteral("片段已复制；当前片段超过缓存上限，已安全保留"));
        }
        return;
    }

    setStatus(QStringLiteral("已导出: %1").arg(QDir::toNativeSeparators(outputPath)));
    emit exportFinished(outputPath);
}

void TrimPanel::onProbeCompleted(const QString& inputPath, const bool success,
                                 const qint64 durationMs, const QString& error) {
    if (inputPath != sourcePath_ || inputPath != probingSourcePath_) {
        return;
    }
    probeThread_.clear();
    probingSourcePath_.clear();
    if (!success || durationMs <= 0) {
        sourceResolutionError_ = error.isEmpty()
            ? QStringLiteral("无法读取当前视频时长")
            : QStringLiteral("无法读取当前视频时长: %1").arg(error);
        setStatus(sourceResolutionError_, true);
        updateActionState();
        return;
    }
    mediaDurationMs_ = durationMs;
    sourceResolutionError_.clear();
    setStatus({});
    updateLabels();
    updateActionState();
    emit sourceChanged(sourcePath_, mediaDurationMs_);
}

void TrimPanel::updateLabels() {
    const RangeSelection range = selection();
    const qint64 absoluteCurrentMs = hostTimeline_ != nullptr
        ? timelineValueToMs(hostTimeline_->value())
        : 0;
    const qint64 selectionDurationMs = range.durationMs();
    const qint64 relativeCurrentMs = std::clamp(
        absoluteCurrentMs - range.startMs, qint64{0}, selectionDurationMs);
    inLabel_->setText(QStringLiteral("%1 / %2")
                          .arg(compactTimestamp(relativeCurrentMs),
                               compactTimestamp(selectionDurationMs)));
    inLabel_->setToolTip(QStringLiteral("选区内位置 %1; 选区时长 %2")
                             .arg(formatTimestampMs(relativeCurrentMs),
                                  formatTimestampMs(selectionDurationMs)));
    outLabel_->setText(formatTimestampMs(range.endMs));
    durationLabel_->setText(QStringLiteral("时长 %1").arg(formatTimestampMs(selectionDurationMs)));
}

void TrimPanel::updatePreviewButtonVisual() {
    const bool dark = usesDarkPalette(this);
    const QString skin = dark ? QStringLiteral("black") : QStringLiteral("white");
    const QString iconName = previewing_
        ? QStringLiteral("icon_suspend.svg")
        : QStringLiteral("icon_play.svg");
    const QString iconPath = billfishResourcePath(
        QStringLiteral("Resources/skin/%1/image/%2").arg(skin, iconName));
    const QIcon icon(iconPath);
    previewButton_->setIcon(icon);
    previewButton_->setIconSize(QSize(28, 28));
    previewButton_->setText(icon.isNull()
                                ? (previewing_ ? QStringLiteral("Ⅱ") : QStringLiteral("▶"))
                                : QString{});
    previewButton_->setToolTip(previewing_ ? QStringLiteral("暂停预览")
                                           : QStringLiteral("预览选区"));
}

void TrimPanel::applyCompactStyle() {
    const bool dark = usesDarkPalette(this);
    QPalette panelPalette = palette();
    panelPalette.setColor(QPalette::Window,
                          dark ? QColor(QStringLiteral("#1F1F1F"))
                               : QColor(QStringLiteral("#FFFFFF")));
    panelPalette.setColor(QPalette::WindowText,
                          dark ? QColor(QStringLiteral("#D9D9D9"))
                               : QColor(QStringLiteral("#525252")));
    setPalette(panelPalette);
    inLabel_->setPalette(panelPalette);
    statusLabel_->setPalette(panelPalette);
    setFont(QApplication::font());

    const QString primaryStyle = primaryToolButtonStyle(dark);
    exportButton_->setStyleSheet(primaryStyle);
    copyButton_->setStyleSheet(primaryStyle);
    cancelButton_->setStyleSheet(QStringLiteral(
        "QToolButton{color:%1;background-color:%2;border:none;border-radius:4px;}"
        "QToolButton:hover{color:%3;background-color:%4;}")
        .arg(dark ? QStringLiteral("#D9D9D9") : QStringLiteral("#525252"),
             dark ? QStringLiteral("#33626262") : QStringLiteral("#40CCCCCC"),
             dark ? QStringLiteral("#FAFAFA") : QStringLiteral("#333333"),
             dark ? QStringLiteral("#4D626262") : QStringLiteral("#66CCCCCC")));
    previewButton_->setStyleSheet(QStringLiteral(
        "QToolButton{border:0;padding:4px;background:transparent;}"
        "QToolButton:hover{background:%1;border-radius:6px;}"
        "QToolButton:disabled{background:transparent;}")
        .arg(dark ? QStringLiteral("#4D626262") : QStringLiteral("#40CCCCCC")));
    progressBar_->setStyleSheet(QStringLiteral(
        "QProgressBar{border:0;background:transparent;}"
        "QProgressBar::chunk{background:#4F7DFF;}"));
}

void TrimPanel::updateActionState() {
    const bool hasTimeline = hostTimeline_ != nullptr && overlay_ != nullptr;
    const bool validRange = hasTimeline && selection().isValid();
    const bool hasSource = !sourcePath_.isEmpty() && QFileInfo::exists(sourcePath_) &&
                           mediaDurationMs_ > 0;
    setInButton_->setEnabled(hasTimeline && !busy_);
    setOutButton_->setEnabled(hasTimeline && !busy_);
    previewButton_->setEnabled(validRange && !busy_);
    modeButton_->setEnabled(!busy_);
    exportButton_->setEnabled(validRange && hasSource && !busy_);
    copyButton_->setEnabled(validRange && hasSource && !busy_);
}

void TrimPanel::seekHostTimeline(const int value) {
    if (hostTimeline_ == nullptr) {
        return;
    }
    const int bounded = std::clamp(value, hostTimeline_->minimum(), hostTimeline_->maximum());
    hostTimeline_->setSliderPosition(bounded);
    emit hostTimeline_->sliderMoved(bounded);
    emit hostTimeline_->sliderReleased();
}

void TrimPanel::clickHostButton(QAbstractButton* button) {
    if (button != nullptr && button->isEnabled()) {
        button->click();
    }
}

void TrimPanel::diagnoseMissingFileIdEmitter(
    QWidget* playerRoot,
    const QList<QObject*>& typedCandidates,
    const int rawSignalObjectCount,
    const int rawSignalMethodCount,
    const int typedSignalMethodCount,
    const bool connectAttempted,
    const bool connectSucceeded,
    const int receiverSlotIndex,
    const QString& receiverSlotSignature) const {
    if (!trimDiagnosticsEnabled()) {
        return;
    }

    QSet<QObject*> objects;
    const auto addObjectTree = [&objects](QObject* root) {
        if (root == nullptr) {
            return;
        }
        objects.insert(root);
        for (QObject* child : root->findChildren<QObject*>()) {
            objects.insert(child);
        }
    };
    addObjectTree(playerRoot);
    for (QWidget* widget : QApplication::allWidgets()) {
        objects.insert(widget);
    }

    QList<QObject*> signalObjects;
    int globalSignalMethodCount = 0;
    for (QObject* object : objects) {
        bool found = false;
        const QMetaObject* meta = object->metaObject();
        for (int index = 0; index < meta->methodCount(); ++index) {
            const QMetaMethod method = meta->method(index);
            if (method.methodType() == QMetaMethod::Signal &&
                method.name() == kFileIdSignalName) {
                found = true;
                ++globalSignalMethodCount;
            }
        }
        if (found) {
            signalObjects.append(object);
        }
    }

    QStringList lines;
    lines.append(QStringLiteral(
        "path-resolution source=diagnostic status=missing-emitter control=%1 host=%2 "
        "localSignalObjects=%3 localSignalMethods=%4 localTypedMethods=%5 typedCandidates=%6 "
        "globalSignalObjects=%7 globalSignalMethods=%8 receiverSlotIndex=%9 receiverSlot=%10 "
        "connectAttempted=%11 connectSucceeded=%12")
                     .arg(objectIdentity(previewControl_))
                     .arg(objectIdentity(playerRoot))
                     .arg(rawSignalObjectCount)
                     .arg(rawSignalMethodCount)
                     .arg(typedSignalMethodCount)
                     .arg(typedCandidates.size())
                     .arg(signalObjects.size())
                     .arg(globalSignalMethodCount)
                     .arg(receiverSlotIndex)
                     .arg(receiverSlotSignature.isEmpty() ? QStringLiteral("none")
                                                          : receiverSlotSignature)
                     .arg(connectAttempted)
                     .arg(connectSucceeded));

    constexpr int kMaximumSignalRecords = 32;
    const int recordCount = std::min(signalObjects.size(), kMaximumSignalRecords);
    for (int index = 0; index < recordCount; ++index) {
        QObject* object = signalObjects.at(index);
        QStringList methods;
        const QMetaObject* meta = object->metaObject();
        for (int methodIndex = 0; methodIndex < meta->methodCount(); ++methodIndex) {
            const QMetaMethod method = meta->method(methodIndex);
            if (method.methodType() != QMetaMethod::Signal ||
                method.name() != kFileIdSignalName) {
                continue;
            }
            QStringList parameterTypes;
            for (int parameterIndex = 0; parameterIndex < method.parameterCount();
                 ++parameterIndex) {
                parameterTypes.append(QStringLiteral("%1:%2")
                                          .arg(QString::fromLatin1(
                                              method.parameterTypes().value(parameterIndex)))
                                          .arg(method.parameterType(parameterIndex)));
            }
            methods.append(QStringLiteral("%1[%2]")
                               .arg(QString::fromLatin1(method.methodSignature()))
                               .arg(parameterTypes.join(QLatin1Char(','))));
        }
        const auto* widget = qobject_cast<const QWidget*>(object);
        lines.append(QStringLiteral("signal=%1 desc=%2 visible=%3 methods=%4 parents=%5")
                         .arg(objectIdentity(object))
                         .arg(isDescendantOf(object, playerRoot))
                         .arg(widget != nullptr ? widget->isVisible() : false)
                         .arg(methods.join(QLatin1Char('|')))
                         .arg(parentChain(object)));
    }
    if (signalObjects.size() > kMaximumSignalRecords) {
        lines.append(QStringLiteral("signal-records-truncated total=%1 cap=%2")
                         .arg(signalObjects.size())
                         .arg(kMaximumSignalRecords));
    }

    constexpr int kMaximumDiagnosticBytes = 8 * 1024;
    constexpr int kMaximumChunkBytes = 1800;
    QList<QString> chunks;
    QString chunk;
    int bytes = 0;
    for (const QString& line : lines) {
        const int lineBytes = line.toUtf8().size();
        if (!chunk.isEmpty() && bytes + lineBytes + 1 > kMaximumChunkBytes) {
            chunks.append(chunk);
            chunk.clear();
            bytes = 0;
        }
        if (lineBytes > kMaximumChunkBytes) {
            chunks.append(line.left(kMaximumChunkBytes / 2));
            continue;
        }
        chunk += (chunk.isEmpty() ? QString{} : QStringLiteral(" ")) + line;
        bytes += lineBytes + 1;
    }
    if (!chunk.isEmpty()) {
        chunks.append(chunk);
    }
    const int maximumChunks = std::min(chunks.size(), kMaximumDiagnosticBytes / kMaximumChunkBytes);
    if (chunks.size() > maximumChunks && maximumChunks > 0) {
        chunks[maximumChunks - 1].append(
            QStringLiteral(" records-truncated chunks=%1 capBytes=%2")
                .arg(chunks.size())
                .arg(kMaximumDiagnosticBytes));
    }
    for (int index = 0; index < maximumChunks; ++index) {
        trimDiagnostic(QStringLiteral("binding-diagnostic part=%1/%2 %3")
                           .arg(index + 1)
                           .arg(maximumChunks)
                           .arg(chunks.at(index)));
    }
}

void TrimPanel::connectFileIdSource(QWidget* playerRoot) {
    QList<QObject*> candidates;
    int rawSignalObjectCount = 0;
    int rawSignalMethodCount = 0;
    int typedSignalMethodCount = 0;
    QObject* object = selectedFileIdSource_.data();
    if (object != nullptr) {
        const QMetaObject* meta = object->metaObject();
        bool foundRawSignal = false;
        for (int index = 0; index < meta->methodCount(); ++index) {
            const QMetaMethod method = meta->method(index);
            if (method.methodType() != QMetaMethod::Signal ||
                method.name() != kFileIdSignalName) {
                continue;
            }
            foundRawSignal = true;
            ++rawSignalMethodCount;
            if (method.parameterCount() == 1 &&
                method.parameterType(0) == QMetaType::LongLong) {
                ++typedSignalMethodCount;
                if (!candidates.contains(object)) {
                    candidates.append(object);
                }
            }
        }
        if (foundRawSignal) {
            ++rawSignalObjectCount;
        }
    }

    QObject* previousEmitter = fileIdEmitter_.data();
    QObject* nextEmitter = candidates.size() == 1 ? candidates.constFirst() : nullptr;
    if (nextEmitter != previousEmitter) {
        if (previousEmitter != nullptr) {
            QObject::disconnect(previousEmitter, nullptr, this, nullptr);
        }
        for (const QPointer<QObject>& source : playbackFrameSources_) {
            if (source != nullptr && source != previousEmitter) {
                QObject::disconnect(source, nullptr, this, nullptr);
            }
        }
        playbackFrameSources_.clear();
        fileIdEmitter_.clear();
        lastObservedFileId_ = -1;
        currentFileId_ = -1;
        if (!explicitInputOverride_) {
            invalidateResolvedSource(nextEmitter == nullptr
                                         ? QStringLiteral("当前播放器没有唯一的视频标识信号")
                                         : QStringLiteral("当前播放器视频标识信号已更换"));
        }
    }
    if (nextEmitter == nullptr) {
        if (diagnoseBindingFailure_ && !explicitInputOverride_) {
            const int receiverSlotIndex = metaObject()->indexOfSlot(
                "onPreviewFileIdChanged(qint64)");
            const QString receiverSlotSignature = receiverSlotIndex >= 0
                ? QString::fromLatin1(metaObject()->method(receiverSlotIndex).methodSignature())
                : QString{};
            diagnoseMissingFileIdEmitter(playerRoot, candidates, rawSignalObjectCount,
                                         rawSignalMethodCount, typedSignalMethodCount, false,
                                         false, receiverSlotIndex, receiverSlotSignature);
        }
        trimDiagnostic(QStringLiteral("path-resolution source=current-id status=%1 candidates=%2")
                           .arg(candidates.isEmpty() ? QStringLiteral("missing-emitter")
                                                      : QStringLiteral("ambiguous-emitter"))
                           .arg(candidates.size()));
        playbackFrameSources_.clear();
        return;
    }

    const int slotIndex = metaObject()->indexOfSlot("onPreviewFileIdChanged(qint64)");
    if (slotIndex < 0) {
        if (diagnoseBindingFailure_ && !explicitInputOverride_) {
            diagnoseMissingFileIdEmitter(playerRoot, candidates, rawSignalObjectCount,
                                         rawSignalMethodCount, typedSignalMethodCount, false,
                                         false, slotIndex, QString{});
        }
        trimDiagnostic(QStringLiteral("path-resolution source=runtime status=receiver-slot-missing"));
        return;
    }
    const QMetaMethod slot = metaObject()->method(slotIndex);
    const QMetaObject* meta = nextEmitter->metaObject();
    bool connectAttempted = false;
    bool connectSucceeded = false;
    for (int index = 0; index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (method.methodType() == QMetaMethod::Signal && method.name() == kFileIdSignalName &&
            method.parameterCount() == 1 && method.parameterType(0) == QMetaType::LongLong) {
            connectAttempted = true;
            if (QObject::connect(nextEmitter, method, this, slot, Qt::UniqueConnection)) {
                connectSucceeded = true;
                fileIdEmitter_ = nextEmitter;
                if (selectedObservedFileId_ > 0) {
                    lastObservedFileId_ = selectedObservedFileId_;
                }
            }
            break;
        }
    }
    if (fileIdEmitter_ == nextEmitter && selectedObservedFileId_ > 0) {
        lastObservedFileId_ = selectedObservedFileId_;
    }
    if (diagnoseBindingFailure_ && !explicitInputOverride_ && fileIdEmitter_ == nullptr) {
        diagnoseMissingFileIdEmitter(
            playerRoot, candidates, rawSignalObjectCount, rawSignalMethodCount,
            typedSignalMethodCount, connectAttempted, connectSucceeded, slotIndex,
            QString::fromLatin1(slot.methodSignature()));
    }
}

void TrimPanel::connectPlaybackFrameSources(QWidget* playerRoot) {
    Q_UNUSED(playerRoot)
    if (fileIdEmitter_ == nullptr) {
        playbackFrameSources_.clear();
        return;
    }
    if (playbackFrameSources_.contains(fileIdEmitter_)) {
        return;
    }

    const int framesSlotIndex = metaObject()->indexOfSlot(
        "onVideoFramesChanged(qint64,qint64)");
    const int totalSlotIndex = metaObject()->indexOfSlot(
        "onVideoTotalFramesChanged(qint64,qint64)");
    if (framesSlotIndex < 0 || totalSlotIndex < 0) {
        return;
    }
    const QMetaMethod framesSlot = metaObject()->method(framesSlotIndex);
    const QMetaMethod totalSlot = metaObject()->method(totalSlotIndex);

    QObject* object = fileIdEmitter_.data();
    bool connectedAny = false;
    const QMetaObject* meta = object->metaObject();
    for (int index = 0; index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (method.methodType() != QMetaMethod::Signal || method.parameterCount() != 2) {
            continue;
        }
        if (method.name() == kVideoFramesSignalName && method.parameterType(0) == QMetaType::LongLong &&
            method.parameterType(1) == QMetaType::LongLong) {
            connectedAny = static_cast<bool>(QObject::connect(
                               object, method, this, framesSlot, Qt::UniqueConnection)) ||
                           connectedAny;
        } else if (method.name() == kVideoTotalFramesSignalName &&
                   method.parameterType(0) == QMetaType::LongLong &&
                   method.parameterType(1) == QMetaType::LongLong) {
            connectedAny = static_cast<bool>(QObject::connect(
                               object, method, this, totalSlot, Qt::UniqueConnection)) ||
                           connectedAny;
        }
    }
    if (connectedAny) {
        playbackFrameSources_.append(object);
    }
}

bool TrimPanel::synchronizeCurrentSource() {
    if (fileIdEmitter_ == nullptr) {
        trimDiagnostic(QStringLiteral("path-resolution source=current-id status=missing-emitter"));
        invalidateResolvedSource(QStringLiteral("无法确认当前视频的唯一标识"));
        return false;
    }
    const qint64 fileId = lastObservedFileId_;
    if (fileId <= 0) {
        trimDiagnostic(QStringLiteral("path-resolution source=current-id status=missing-value"));
        invalidateResolvedSource(QStringLiteral("无法读取当前视频标识，已拒绝剪辑"));
        return false;
    }
    lastObservedFileId_ = fileId;
    trimDiagnostic(QStringLiteral("path-resolution source=current-id status=selected fileId=%1")
                       .arg(fileId));
    return resolveFileId(fileId);
}

bool TrimPanel::resolveFileId(const qint64 fileId) {
    if (fileId <= 0) {
        invalidateResolvedSource(QStringLiteral("当前视频标识无效，已拒绝剪辑"));
        return false;
    }
    const QString libraryRoot = currentLibraryRoot_ ? currentLibraryRoot_().trimmed() : QString();
    if (libraryRoot.isEmpty()) {
        sourceResolutionError_ = QStringLiteral("当前素材库尚未就绪，无法定位当前视频");
        trimDiagnostic(QStringLiteral("path-resolution source=file-id status=missing-library fileId=%1 error=%2")
                           .arg(fileId)
                           .arg(sourceResolutionError_));
        setStatus(sourceResolutionError_, true);
        invalidateResolvedSource(sourceResolutionError_);
        return false;
    }
    const QString sqliteLibrary = BillfishFileLocator::findSqliteLibrary(
        QCoreApplication::applicationDirPath());
    const BillfishFileLookup lookup = BillfishFileLocator(libraryRoot, sqliteLibrary).locate(fileId);
    if (!lookup.success || !lookup.file.exists) {
        sourceResolutionError_ = lookup.error.isEmpty()
            ? QStringLiteral("当前素材文件不存在")
            : lookup.error;
        trimDiagnostic(QStringLiteral("path-resolution source=file-id status=failed fileId=%1 root=%2 database=%3 exists=%4 path=%5 error=%6")
                           .arg(fileId)
                           .arg(libraryRoot)
                           .arg(sqliteLibrary)
                           .arg(lookup.file.exists)
                           .arg(lookup.file.absolutePath, sourceResolutionError_));
        invalidateResolvedSource(sourceResolutionError_);
        return false;
    }
    const bool idChanged = currentFileId_ != fileId;
    currentFileId_ = fileId;
    if (idChanged) {
        currentVideoFrame_ = -1;
        totalVideoFrames_ = -1;
    }
    trimDiagnostic(QStringLiteral("path-resolution source=file-id status=located fileId=%1 root=%2 database=%3 path=%4 durationMs=%5")
                       .arg(fileId)
                       .arg(libraryRoot)
                       .arg(sqliteLibrary)
                       .arg(lookup.file.absolutePath)
                       .arg(lookup.file.durationMs));
    setResolvedSource(lookup.file.absolutePath, lookup.file.durationMs);
    return true;
}

void TrimPanel::invalidateResolvedSource(const QString& error) {
    const bool hadSource = !sourcePath_.isEmpty() || mediaDurationMs_ > 0 || currentFileId_ > 0;
    sourcePath_.clear();
    mediaDurationMs_ = -1;
    currentFileId_ = -1;
    currentVideoFrame_ = -1;
    totalVideoFrames_ = -1;
    sourceResolutionError_ = error;
    if (overlay_ != nullptr) {
        overlay_->resetToFullRange();
    }
    sourceLabel_->setText(QStringLiteral("等待当前视频"));
    sourceLabel_->setToolTip({});
    setStatus(error, true);
    updateLabels();
    updateActionState();
    if (hadSource) {
        emit sourceChanged({}, -1);
    }
}

void TrimPanel::setResolvedSource(const QString& path, const qint64 durationMs) {
    const QString resolvedPath = QFileInfo(path).absoluteFilePath();
    const bool sourcePathChanged = QString::compare(sourcePath_, resolvedPath,
                                                    Qt::CaseInsensitive) != 0;
    sourcePath_ = resolvedPath;
    mediaDurationMs_ = durationMs > 0 ? durationMs : -1;
    if (sourcePathChanged) {
        currentVideoFrame_ = -1;
        totalVideoFrames_ = -1;
    }
    sourceResolutionError_.clear();
    trimDiagnostic(QStringLiteral("path-resolution source=resolved status=applied fileId=%1 path=%2 durationMs=%3")
                       .arg(currentFileId_)
                       .arg(sourcePath_)
                       .arg(mediaDurationMs_));
    sourceLabel_->setText(QFileInfo(sourcePath_).fileName());
    sourceLabel_->setToolTip(QDir::toNativeSeparators(sourcePath_));
    if (overlay_ != nullptr) {
        overlay_->resetToFullRange();
    }
    if (mediaDurationMs_ > 0) {
        setStatus({});
    } else {
        startDurationProbe();
    }
    updateLabels();
    updateActionState();
    emit sourceChanged(sourcePath_, mediaDurationMs_);
}

void TrimPanel::startDurationProbe() {
    const FfmpegTools tools = locateFfmpegTools(QCoreApplication::applicationDirPath());
    if (!tools.canProbe()) {
        sourceResolutionError_ = QStringLiteral("无法读取视频时长: 未找到 FFprobe");
        setStatus(sourceResolutionError_, true);
        updateActionState();
        return;
    }
    probingSourcePath_ = sourcePath_;
    auto* thread = new QThread;
    auto* worker = new MediaProbeWorker(tools, sourcePath_);
    worker->moveToThread(thread);
    probeThread_ = thread;
    connect(thread, &QThread::started, worker, &MediaProbeWorker::run);
    connect(worker, &MediaProbeWorker::completed,
            this, &TrimPanel::onProbeCompleted, Qt::QueuedConnection);
    connect(worker, &MediaProbeWorker::completed, worker, &QObject::deleteLater);
    connect(worker, &MediaProbeWorker::completed, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    setStatus(QStringLiteral("正在读取视频时长..."));
    updateActionState();
    thread->start();
}

void TrimPanel::setStatus(const QString& text, const bool error) {
    statusLabel_->setText(text);
    statusLabel_->setToolTip(text);
    statusLabel_->setStyleSheet(error ? QStringLiteral("color:#ff7777;") : QString{});
    statusLabel_->setVisible(!text.isEmpty());
}

void TrimPanel::setBusy(const bool busy, const QString& text) {
    busy_ = busy;
    progressBar_->setVisible(busy);
    cancelButton_->setVisible(busy);
    cancelButton_->setEnabled(busy);
    if (busy) {
        progressBar_->setRange(0, 0);
        progressBar_->setFormat(text);
        setStatus(text);
        stopPreview();
    }
    updateActionState();
}

bool TrimPanel::startTrim(TrimRequest request, const JobPurpose purpose) {
    if (sourcePath_.isEmpty() || !QFileInfo(sourcePath_).isFile()) {
        setStatus(sourceResolutionError_.isEmpty() ? QStringLiteral("未找到当前视频文件") : sourceResolutionError_, true);
        return false;
    }
    request.inputPath = sourcePath_;
    request.range = selection();
    request.mode = trimMode();
    request.overwrite = true;
    const QString validationError = validateTrimRequest(request, mediaDurationMs_);
    if (!validationError.isEmpty()) {
        setStatus(validationError, true);
        return false;
    }
    const FfmpegTools tools = locateFfmpegTools(QCoreApplication::applicationDirPath());
    if (!tools.isComplete()) {
        setStatus(QStringLiteral("未找到完整 FFmpeg/FFprobe，请设置 BILLFISH_FFMPEG 与 BILLFISH_FFPROBE"), true);
        return false;
    }

    auto* thread = new QThread;
    auto* worker = new TrimExportWorker(tools, request);
    worker->moveToThread(thread);
    worker_ = worker;
    workerThread_ = thread;
    activeRequest_ = request;
    activePurpose_ = purpose;
    connect(thread, &QThread::started, worker, &TrimExportWorker::run);
    connect(worker, &TrimExportWorker::progressChanged,
            this, &TrimPanel::onWorkerProgress, Qt::QueuedConnection);
    connect(worker, &TrimExportWorker::completed,
            this, &TrimPanel::onWorkerCompleted, Qt::QueuedConnection);
    connect(worker, &TrimExportWorker::completed, worker, &QObject::deleteLater);
    connect(worker, &TrimExportWorker::completed, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    setBusy(true, purpose == JobPurpose::Copy
                      ? QStringLiteral("正在生成可复制片段")
                      : QStringLiteral("正在导出片段"));
    thread->start();
    return true;
}

bool TrimPanel::copyFileToClipboard(const QString& outputPath) {
    const ClipboardResult result = WindowsFileClipboard::setFiles({outputPath}, DropEffect::Copy);
    if (!result.success) {
        setStatus(QStringLiteral("复制片段失败: %1").arg(result.error), true);
        emit operationFailed(result.error);
        return false;
    }
    setStatus(QStringLiteral("片段已复制，可直接粘贴到文件夹或聊天窗口"));
    emit copyFinished(outputPath);
    return true;
}

TrimRequest TrimPanel::makeRequest(const QString& outputPath) const {
    TrimRequest request;
    request.inputPath = sourcePath_;
    request.outputPath = outputPath;
    request.range = selection();
    request.mode = trimMode();
    request.overwrite = true;
    return request;
}

QString TrimPanel::defaultExportPath() const {
    const QFileInfo input(sourcePath_);
    const RangeSelection range = selection();
    QString extension = input.suffix();
    if (extension.isEmpty()) {
        extension = QStringLiteral("mp4");
    }
    const QString fileName = QStringLiteral("%1_clip_%2-%3.%4")
                                 .arg(input.completeBaseName())
                                 .arg(range.startMs)
                                 .arg(range.endMs)
                                 .arg(extension);
    return QDir(input.absolutePath()).filePath(fileName);
}

QString TrimPanel::cacheRoot() const {
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return QDir(root).filePath(QStringLiteral("billfish-video-trim"));
}

qint64 TrimPanel::timelineValueToMs(const int value) const noexcept {
    if (hostTimeline_ == nullptr) {
        return 0;
    }
    const int minimum = hostTimeline_->minimum();
    const int maximum = hostTimeline_->maximum();
    if (mediaDurationMs_ > 0 && maximum > minimum) {
        const double fraction = static_cast<double>(value - minimum) / static_cast<double>(maximum - minimum);
        return static_cast<qint64>(std::llround(std::clamp(fraction, 0.0, 1.0) * mediaDurationMs_));
    }
    return std::max<qint64>(0, static_cast<qint64>(value) - minimum);
}

QSlider* TrimPanel::findHostTimeline(QWidget* playerRoot) {
    if (auto* exact = playerRoot->findChild<QSlider*>(QString::fromLatin1(kTimelineObjectName),
                                                      Qt::FindChildrenRecursively)) {
        return exact;
    }
    const auto sliders = playerRoot->findChildren<QSlider*>();
    for (QSlider* slider : sliders) {
        if (slider->inherits("MediaSlider")) {
            return slider;
        }
    }
    return nullptr;
}

QAbstractButton* TrimPanel::findButton(QWidget* playerRoot, const QString& objectName) {
    return playerRoot->findChild<QAbstractButton*>(objectName, Qt::FindChildrenRecursively);
}

}  // namespace billfish::trim
