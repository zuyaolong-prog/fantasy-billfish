#include "automation/video_capture.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <memory>

namespace billfish::automation {
namespace {

class CaptureDialog final : public QDialog {
public:
    explicit CaptureDialog(QWidget* parent) : QDialog(parent) {}
    std::function<void()> cancelRequested;
    bool importing = false;
    void reject() override
    {
        if (importing) return;
        const auto callback = cancelRequested;
        if (callback) callback();
        else QDialog::reject();
    }
};

bool webUrl(const QString& value)
{
    const QUrl url(value);
    return url.isValid() && !url.host().isEmpty() &&
        (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
}

bool extractorPage(const QString& value)
{
    const QString host = QUrl(value).host().toLower();
    return host == QStringLiteral("youtu.be") || host == QStringLiteral("youtube.com") ||
        host.endsWith(QStringLiteral(".youtube.com")) || host == QStringLiteral("bilibili.com") ||
        host.endsWith(QStringLiteral(".bilibili.com")) || host == QStringLiteral("b23.tv");
}

QString normalizedRoot(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}

} // namespace

struct VideoCaptureController::Capture {
    enum class Stage { Probing, Choosing, Downloading, Importing, Canceling };
    Stage stage = Stage::Probing;
    QString libraryRoot;
    QString origin;
    QString title;
    bool preserveProvidedTitle = false;
    QString cancellationCode;
    QString cancellationMessage;
    std::unique_ptr<QTemporaryDir> workspace;
    QPointer<CaptureDialog> dialog;
    QComboBox* choices = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* status = nullptr;
    QProgressBar* bar = nullptr;
    QPushButton* download = nullptr;
    QPushButton* cancelButton = nullptr;
};

VideoCaptureController::VideoCaptureController(QObject* parent) : QObject(parent) {}
VideoCaptureController::~VideoCaptureController()
{
    shutdown();
    // Destruction cannot wait for a native importer or downloader. Preserve
    // their files if the host has not acknowledged the lease before teardown.
    for (const auto& capture : captures_) {
        if (capture->workspace && capture->stage != Capture::Stage::Choosing)
            capture->workspace->setAutoRemove(false);
    }
}

void VideoCaptureController::setMainWindow(QWidget* window) { mainWindow_ = window; }
bool VideoCaptureController::has(const QString& id) const { return captures_.contains(id); }

void VideoCaptureController::start(const QString& id, const QJsonObject& args, const QString& libraryRoot)
{
    if (captures_.contains(id)) return;
    const QString url = args.value(QStringLiteral("url")).toString();
    const QString page = args.value(QStringLiteral("page_url")).toString();
    const QString media = args.value(QStringLiteral("media_url")).toString();
    const QString origin = webUrl(page) ? page : url;
    const QString source = extractorPage(url) ? url : extractorPage(page) ? page :
        webUrl(media) ? media : url;
    if (stopping_ || libraryRoot.isEmpty() || !webUrl(source) || !mainWindow_) {
        emit failed(id, QStringLiteral("VIDEO_CAPTURE_UNAVAILABLE"),
                    QStringLiteral("无法开始视频采集, 请先打开资料库并使用有效的视频链接."), {});
        return;
    }

    auto capture = QSharedPointer<Capture>::create();
    capture->libraryRoot = libraryRoot;
    capture->origin = origin;
    capture->title = args.value(QStringLiteral("title")).toString();
    capture->preserveProvidedTitle = !extractorPage(source) && !capture->title.trimmed().isEmpty();
    capture->workspace = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/Billfish-video-capture-XXXXXX"));
    if (!capture->workspace->isValid()) {
        emit failed(id, QStringLiteral("TEMP_DIRECTORY_FAILED"), QStringLiteral("无法创建下载临时目录."), {});
        return;
    }
    auto* dialog = new CaptureDialog(mainWindow_);
    capture->dialog = dialog;
    dialog->setObjectName(QStringLiteral("BillfishVideoCaptureDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("采集视频"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->resize(510, 230);
    auto* layout = new QVBoxLayout(dialog);
    capture->titleLabel = new QLabel(capture->title.isEmpty() ? QStringLiteral("网页视频") : capture->title, dialog);
    capture->titleLabel->setWordWrap(true);
    layout->addWidget(capture->titleLabel);
    auto* target = new QLabel(QStringLiteral("保存到当前资料库的浏览器采集文件夹"), dialog);
    layout->addWidget(target);
    capture->choices = new QComboBox(dialog);
    capture->choices->setObjectName(QStringLiteral("videoCaptureResolution"));
    capture->choices->setEnabled(false);
    layout->addWidget(capture->choices);
    capture->status = new QLabel(QStringLiteral("正在读取可用分辨率..."), dialog);
    capture->status->setWordWrap(true);
    layout->addWidget(capture->status);
    capture->bar = new QProgressBar(dialog);
    capture->bar->setRange(0, 0);
    layout->addWidget(capture->bar);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    capture->download = new QPushButton(QStringLiteral("下载并入库"), dialog);
    capture->download->setObjectName(QStringLiteral("videoCaptureDownload"));
    capture->download->setEnabled(false);
    capture->cancelButton = new QPushButton(QStringLiteral("取消"), dialog);
    capture->cancelButton->setObjectName(QStringLiteral("videoCaptureCancel"));
    buttons->addWidget(capture->download);
    buttons->addWidget(capture->cancelButton);
    layout->addLayout(buttons);
    dialog->cancelRequested = [this, id] {
        cancel(id, QStringLiteral("CANCELED"), QStringLiteral("视频采集已取消."));
    };
    connect(capture->cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(capture->download, &QPushButton::clicked, this, [this, id] { confirm(id); });
    // A parent window disappearing cannot leave a downloader without a visible owner.
    connect(dialog, &QObject::destroyed, this, [this, id] {
        cancel(id, QStringLiteral("CANCELED"), QStringLiteral("视频采集窗口已关闭."));
    });
    captures_.insert(id, capture);
    dialog->show();
    dialog->raise();
    emit progress(id, 0, 0, QStringLiteral("正在读取可用分辨率"));
    QJsonObject request{{QStringLiteral("mode"), QStringLiteral("probe_video")},
                        {QStringLiteral("url"), source},
                        {QStringLiteral("workspace"), capture->workspace->path()}};
    if (webUrl(origin)) request.insert(QStringLiteral("referer"), origin);
    emit transferRequested(id, request);
}

void VideoCaptureController::confirm(const QString& id)
{
    const auto capture = captures_.value(id);
    if (!capture || capture->stage != Capture::Stage::Choosing || !capture->dialog) return;
    const QString choice = capture->choices->currentData().toString();
    if (choice.isEmpty()) return;
    capture->stage = Capture::Stage::Downloading;
    capture->download->setEnabled(false);
    capture->choices->setEnabled(false);
    capture->bar->show();
    capture->bar->setRange(0, 0);
    capture->status->setText(QStringLiteral("正在下载视频..."));
    emit progress(id, 0, 0, QStringLiteral("正在下载视频"));
    emit transferRequested(id, {{QStringLiteral("mode"), QStringLiteral("download_video")},
                                {QStringLiteral("workspace"), capture->workspace->path()},
                                {QStringLiteral("choice_id"), choice}});
}

void VideoCaptureController::transferFinished(QString id, QJsonObject data)
{
    const auto capture = captures_.value(id);
    if (!capture) return;
    if (capture->stage == Capture::Stage::Canceling) {
        finish(id, false, {}, capture->cancellationCode, capture->cancellationMessage);
        return;
    }
    if (!capture->dialog) {
        finish(id, false, {}, QStringLiteral("CANCELED"), QStringLiteral("视频采集窗口已关闭."));
        return;
    }
    if (capture->stage == Capture::Stage::Probing) {
        const auto choices = data.value(QStringLiteral("choices")).toArray();
        for (const auto& value : choices) {
            const auto choice = value.toObject();
            const auto key = choice.value(QStringLiteral("choice_id")).toString();
            const auto label = choice.value(QStringLiteral("label")).toString();
            if (!key.isEmpty() && !label.isEmpty()) capture->choices->addItem(label, key);
        }
        if (capture->choices->count() == 0) {
            finish(id, false, {}, QStringLiteral("NO_VIDEO_FORMATS"), QStringLiteral("这个链接没有可下载的视频分辨率."));
            return;
        }
        const auto title = data.value(QStringLiteral("title")).toString();
        if (!capture->preserveProvidedTitle && !title.isEmpty()) capture->title = title;
        capture->titleLabel->setText(capture->title.isEmpty() ? QStringLiteral("网页视频") : capture->title);
        capture->stage = Capture::Stage::Choosing;
        capture->choices->setEnabled(true);
        capture->download->setEnabled(true);
        capture->status->setText(QStringLiteral("请选择分辨率, 确认后开始下载."));
        capture->bar->hide();
        emit progress(id, 1, 1, QStringLiteral("请选择分辨率, 确认后开始下载."));
        return;
    }
    if (capture->stage != Capture::Stage::Downloading) return;
    const QString path = data.value(QStringLiteral("path")).toString();
    const QFileInfo output(path);
    const QString relative = QDir(capture->workspace->path()).relativeFilePath(output.absoluteFilePath());
    if (path.isEmpty() || !output.isFile() || output.size() <= 0 || relative == QStringLiteral("..") ||
        relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)) {
        finish(id, false, {}, QStringLiteral("DOWNLOAD_OUTPUT_INVALID"), QStringLiteral("视频下载未生成有效文件."));
        return;
    }
    capture->stage = Capture::Stage::Importing;
    capture->dialog->importing = true;
    capture->cancelButton->setEnabled(false);
    capture->bar->show();
    capture->bar->setRange(0, 0);
    capture->status->setText(QStringLiteral("正在加入浏览器采集文件夹..."));
    const QString title = data.value(QStringLiteral("title")).toString();
    if (!capture->preserveProvidedTitle && !title.isEmpty()) capture->title = title;
    emit progress(id, 0, 0, QStringLiteral("正在加入浏览器采集文件夹"));
    emit importRequested(id, output.absoluteFilePath(), capture->title, capture->origin, capture->libraryRoot);
}

void VideoCaptureController::transferFailed(QString id, QString code, QString message, QJsonObject details)
{
    const auto capture = captures_.value(id);
    if (!capture || capture->stage == Capture::Stage::Importing) return;
    if (capture->stage == Capture::Stage::Canceling) {
        code = capture->cancellationCode;
        message = capture->cancellationMessage;
    }
    finish(id, false, {}, code, message, details);
}

void VideoCaptureController::transferProgress(QString id, int completed, int total, QString message)
{
    const auto capture = captures_.value(id);
    if (!capture || capture->stage == Capture::Stage::Canceling || capture->stage == Capture::Stage::Importing) return;
    if (capture->dialog) {
        capture->bar->show();
        capture->bar->setRange(0, total > 0 ? total : 0);
        if (total > 0) capture->bar->setValue(qBound(0, completed, total));
        capture->status->setText(message);
    }
    emit progress(id, completed, total, message);
}

void VideoCaptureController::finishImport(QString id, QJsonObject data)
{
    const auto capture = captures_.value(id);
    if (capture && capture->stage == Capture::Stage::Importing) {
        if (!data.value(QStringLiteral("source_safe_to_delete")).toBool())
            capture->workspace->setAutoRemove(false);
        finish(id, true, data);
    }
}

void VideoCaptureController::failImport(QString id, QString code, QString message, QJsonObject details)
{
    const auto capture = captures_.value(id);
    if (capture && capture->stage == Capture::Stage::Importing) {
        if (!details.value(QStringLiteral("source_safe_to_delete")).toBool())
            capture->workspace->setAutoRemove(false);
        finish(id, false, {}, code, message, details);
    }
}

void VideoCaptureController::cancel(const QString& id, const QString& code, const QString& message)
{
    const auto capture = captures_.value(id);
    if (!capture || capture->stage == Capture::Stage::Importing || capture->stage == Capture::Stage::Canceling) return;
    if (capture->stage == Capture::Stage::Choosing) {
        finish(id, false, {}, code, message);
        return;
    }
    capture->stage = Capture::Stage::Canceling;
    capture->cancellationCode = code;
    capture->cancellationMessage = message;
    if (capture->dialog) {
        capture->download->setEnabled(false);
        capture->cancelButton->setEnabled(false);
        capture->status->setText(QStringLiteral("正在停止视频采集..."));
    }
    // The transport reports completion only after its process has exited. Keep
    // the workspace alive until that callback, even after the user cancels.
    emit transferCancelRequested(id);
}

void VideoCaptureController::finish(const QString& id, bool success, QJsonObject data,
                                    const QString& code, const QString& message, QJsonObject details)
{
    const auto capture = captures_.take(id);
    if (!capture) return;
    if (capture->dialog) {
        auto* dialog = capture->dialog.data();
        dialog->importing = false;
        dialog->cancelRequested = {};
        capture->bar->hide();
        capture->download->hide();
        capture->choices->setEnabled(false);
        capture->cancelButton->setEnabled(true);
        capture->cancelButton->setText(QStringLiteral("关闭"));
        capture->status->setText(success ? QStringLiteral("视频已加入浏览器采集文件夹.") : message);
        if (stopping_ || code == QStringLiteral("CANCELED")) dialog->close();
    }
    if (success) emit finished(id, data);
    else emit failed(id, code, message, details);
}

void VideoCaptureController::libraryChanged(const QString& libraryRoot)
{
    const auto ids = captures_.keys();
    for (const auto& id : ids) {
        const auto capture = captures_.value(id);
        if (capture && normalizedRoot(capture->libraryRoot) != normalizedRoot(libraryRoot))
            cancel(id, QStringLiteral("LIBRARY_CHANGED"), QStringLiteral("资料库已切换, 请在当前资料库重新采集视频."));
    }
}

void VideoCaptureController::shutdown()
{
    if (stopping_) return;
    stopping_ = true;
    const auto ids = captures_.keys();
    for (const auto& id : ids) cancel(id, QStringLiteral("CANCELED"), QStringLiteral("视频采集已停止."));
}

} // namespace billfish::automation
