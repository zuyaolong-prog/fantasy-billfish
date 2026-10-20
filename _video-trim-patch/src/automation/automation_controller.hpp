#pragma once

#include <QHash>
#include <QDeadlineTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>

class QProcess;

namespace billfish::automation {
class RuntimeHost;
class NativeAdapter;
class PluginManager;
class AutomationUi;
class MediaTransferProcess;
class VideoCaptureController;
class FormatPreviewBridge;

class AutomationController final : public QObject {
    Q_OBJECT
public:
    explicit AutomationController(RuntimeHost* host, QObject* parent = nullptr);

private:
    struct Call {
        QJsonObject envelope;
        QString libraryRoot;
        bool pluginSdk = false;
        bool job = false;
        bool alreadyReplied = false;
    };
    struct Job {
        QByteArray fingerprint;
        QJsonObject data;
    };
    struct Verification {
        QJsonObject nativeResult;
        QJsonObject lastActual;
        QDeadlineTimer deadline;
        int attempts = 0;
    };
    struct Import {
        QJsonObject intent;
        QJsonObject nativeResult;
        QString workspace;
        QString abortCode;
        QString abortMessage;
        QJsonObject abortDetails;
        QJsonObject preparedFolder;
        QJsonObject folderArguments;
        QJsonObject folderLastError;
        QDeadlineTimer folderDeadline;
        int folderReadAttempts = 0;
        bool waitingForFolder = false;
        QPointer<MediaTransferProcess> transfer;
        bool nativePending = false;
        bool sourceSafe = true;
        bool videoCapture = false;
    };
    struct Replacement {
        QJsonObject intent;
        QJsonObject lastActual;
        QDeadlineTimer deadline;
        QString abortCode;
        QString abortMessage;
        QJsonObject abortDetails;
        QString commitState = QStringLiteral("not_started");
        QString helperMode;
        bool sourceSafe = true;
    };
    QJsonObject currentInfo();
    void dispatch(const QString& id, const QJsonObject& request, bool pluginSdk = false);
    void runHelper(const QString& id, const QJsonObject& request, const QString& mode, int timeoutMs = 30000);
    void nativeFinished(const QString& id, const QJsonObject& data);
    void startVerification(const QString& id);
    void acceptVerification(const QString& id, const QJsonObject& data);
    void beginImport(const QString& id);
    bool replacementOwnerMatches(const QString& id);
    void verifyReplacement(const QString& id);
    void downloadImage(const QString& id, const QJsonObject& validated);
    void submitImport(const QString& id, const QJsonObject& intent);
    void continuePreparedFolder(const QString& id);
    bool importOwnerMatches(const QString& id);
    QJsonObject releaseImport(const QString& id);
    void startMediaTransfer(const QString& id, const QJsonObject& request, bool video);
    void succeed(const QString& id, QJsonObject data);
    void fail(const QString& id, const QString& code, const QString& message,
              const QJsonObject& details = QJsonObject());
    void progress(const QString& id, int completed, int total, const QString& message);
    void ensureUiJob(const QString& id);
    void retainCompleted(const QString& id);
    bool bindLibrary(const QString& id);
    void sendSuccess(const QString& id, const QJsonObject& data, bool pluginSdk);
    void sendError(const QString& id, const QString& code, const QString& message, bool pluginSdk,
                   const QJsonObject& details = QJsonObject());

    RuntimeHost* host_;
    NativeAdapter* native_;
    PluginManager* plugins_;
    AutomationUi* ui_;
    VideoCaptureController* video_;
    FormatPreviewBridge* formats_;
    QHash<QString, Call> calls_;
    QHash<QString, Job> jobs_;
    QHash<QString, Verification> verifications_;
    QHash<QString, QPointer<QProcess>> verificationProcesses_;
    QHash<QString, QJsonObject> selectionSnapshots_;
    QHash<QString, Import> imports_;
    QHash<QString, QPointer<QProcess>> importHelpers_;
    QHash<QString, Replacement> replacements_;
    QHash<QString, QPointer<QProcess>> replacementHelpers_;
    QHash<QString, QPointer<MediaTransferProcess>> videoTransfers_;
    QList<QString> completedJobs_;
};
} // namespace billfish::automation
