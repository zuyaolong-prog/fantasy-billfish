#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>

#include <memory>

class IResponseInfo;
class QEvent;
class QAbstractItemView;
class QModelIndex;
struct BFTaskResultInfo;

namespace billfish::automation {

class NativeAdapterPrivate;

class NativeAdapter final : public QObject {
    Q_OBJECT
public:
    explicit NativeAdapter(QObject* parent = nullptr);
    ~NativeAdapter() override;

    QJsonObject info();
    QByteArray libraryIdentity();
    QJsonObject selectedItems();
    QAbstractItemView* currentMaterialView();
    QJsonObject itemForIndex(const QModelIndex& index);
    void setFileCover(const QString& requestId, const QString& libraryRoot,
                      const QString& itemId, const QString& coverPath);
    void addFileTypes(const QString& requestId, const QString& libraryRoot, const QStringList& extensions);
    QJsonObject refreshCurrentSearch();
    QJsonObject syncLibraryAfterTypeRegistration(const QString& libraryRoot);
    QJsonObject fileTypeRegistrationReadiness(const QString& libraryRoot);
    QJsonObject prepareBrowserCaptureFolder(const QString& libraryRoot, const QJsonObject& preflight);
    void importFile(const QString& requestId, const QJsonObject& request);
    void cancelImport(const QString& requestId);
    void execute(const QString& requestId, const QJsonObject& request);

signals:
    void activeLibraryChanged(const QString& libraryRoot);
    void finished(const QString& requestId, const QJsonObject& data);
    void failed(const QString& requestId, const QString& code, const QString& message, const QJsonObject& details);
    void progress(const QString& requestId, int completed, int total, const QString& message);

private slots:
    void observeLibrary();
    void libraryLoaded(int code, const QString& message);
    void fileResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response);
    void tagResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response);
    void typeResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response);
    void fileProgress(qlonglong actionId, double fraction, int total, int already);
    void libraryClosed();
    void continueRequest();
    void verifyOwner();
    void importTaskResult(const BFTaskResultInfo& result);
    void importQueueFinished(int announcedTotal, int success, int failed);
    void importProgress(int completed, int total);
    void drainImport();
    void importWorkerFinished();
    void importRenameResult(qlonglong actionId, QSharedPointer<const IResponseInfo> response);
    void finishImport();
    void nativeShutdownStarted();
    void continueNativeShutdown();

private:
    friend class NativeAdapterPrivate;
    std::unique_ptr<NativeAdapterPrivate> d_;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace billfish::automation
