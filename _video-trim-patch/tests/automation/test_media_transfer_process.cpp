#include "automation/media_transfer_process.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using billfish::automation::MediaTransferProcess;

class MediaTransferTest final : public QObject {
    Q_OBJECT
private:
    QString helperPath_;
    QByteArray previousHelper_;
    QByteArray previousPython_;
    bool helperExisted_ = false;
    bool fixtureTouched_ = false;

    static QJsonObject request(const QString& mode, const QString& workspace) {
        return {{QStringLiteral("mode"), mode}, {QStringLiteral("workspace"), workspace}};
    }

private slots:
    void initTestCase() {
        previousPython_ = qgetenv("BILLFISH_PYTHON");
        QString python = QString::fromUtf8(previousPython_);
        if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
        if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
        QVERIFY(!python.isEmpty());
        qputenv("BILLFISH_PYTHON", python.toUtf8());
        const QString directory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("automation"));
        QVERIFY(QDir().mkpath(directory));
        helperPath_ = QDir(directory).filePath(QStringLiteral("media_transfer.py"));
        QFile previous(helperPath_);
        helperExisted_ = previous.exists();
        if (helperExisted_) { QVERIFY(previous.open(QIODevice::ReadOnly)); previousHelper_ = previous.readAll(); previous.close(); }
        QFile fixture(helperPath_);
        QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fixtureTouched_ = true;
        const QByteArray script = R"PY(import json,sys,time
from pathlib import Path
r=json.load(sys.stdin)
def emit(v):
 print(json.dumps(v,separators=(',',':')),flush=True)
mode=r['mode']
if mode=='late_result':
 emit({'event':'result','ok':True,'result':{'value':7}})
 time.sleep(.25)
 Path(r['workspace'],'exit-marker').write_text('done')
elif mode=='wait':
 emit({'event':'progress','stage':'waiting','percent':0})
 time.sleep(60)
elif mode=='duplicate':
 emit({'event':'result','ok':True,'result':{'value':7}})
 emit({'event':'result','ok':True,'result':{'value':8}})
)PY";
        QCOMPARE(fixture.write(script), qint64(script.size()));
    }

    void cleanupTestCase() {
        if (fixtureTouched_) {
            if (helperExisted_) {
                QFile helper(helperPath_);
                QVERIFY(helper.open(QIODevice::WriteOnly | QIODevice::Truncate));
                QCOMPARE(helper.write(previousHelper_), qint64(previousHelper_.size()));
            } else QVERIFY(QFile::remove(helperPath_));
        }
        if (previousPython_.isNull()) qunsetenv("BILLFISH_PYTHON"); else qputenv("BILLFISH_PYTHON", previousPython_);
    }

    void waitsForProcessExitBeforePublishingSuccess() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        MediaTransferProcess process;
        QSignalSpy completed(&process, &MediaTransferProcess::finished);
        QSignalSpy errors(&process, &MediaTransferProcess::failed);
        bool markerPresentAtCompletion = false;
        connect(&process, &MediaTransferProcess::finished, this, [&](const QJsonObject&) {
            markerPresentAtCompletion = QFileInfo::exists(QDir(workspace.path()).filePath(QStringLiteral("exit-marker")));
        });
        QVERIFY(process.start(request(QStringLiteral("late_result"), workspace.path()), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        QVERIFY(markerPresentAtCompletion);
        QVERIFY(!process.isRunning());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(completed.first().at(0).toJsonObject().value(QStringLiteral("value")).toInt(), 7);
    }

    void cancellationDrainsAndCanBeReused() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        MediaTransferProcess process;
        QSignalSpy progress(&process, &MediaTransferProcess::progress);
        QSignalSpy errors(&process, &MediaTransferProcess::failed);
        QSignalSpy completed(&process, &MediaTransferProcess::finished);
        QVERIFY(process.start(request(QStringLiteral("wait"), workspace.path()), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(progress.count(), 1, 5000);
        process.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 5000);
        QCOMPARE(errors.first().at(0).toString(), QStringLiteral("media_transfer_cancelled"));
        QVERIFY(!process.isRunning());
        QCOMPARE(completed.count(), 0);
        QVERIFY(process.start(request(QStringLiteral("late_result"), workspace.path()), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        QCOMPARE(errors.count(), 1);
    }

    void timeoutIsFailureAndDuplicateTerminalIsNotSuccess() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        MediaTransferProcess process;
        QSignalSpy errors(&process, &MediaTransferProcess::failed);
        QSignalSpy completed(&process, &MediaTransferProcess::finished);
        QVERIFY(process.start(request(QStringLiteral("wait"), workspace.path()), 300));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 5000);
        QCOMPARE(errors.first().at(0).toString(), QStringLiteral("media_transfer_timeout"));
        QVERIFY(!process.isRunning());
        QVERIFY(process.start(request(QStringLiteral("duplicate"), workspace.path()), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 2, 5000);
        QCOMPARE(errors.last().at(0).toString(), QStringLiteral("media_transfer_protocol_error"));
        QCOMPARE(completed.count(), 0);
    }

    void canRunAgainAfterFailedStart() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        MediaTransferProcess process;
        QSignalSpy errors(&process, &MediaTransferProcess::failed);
        QSignalSpy completed(&process, &MediaTransferProcess::finished);
        const QByteArray python = qgetenv("BILLFISH_PYTHON");
        qputenv("BILLFISH_PYTHON", QDir(workspace.path()).filePath(QStringLiteral("missing-python.exe")).toUtf8());
        const bool started = process.start(request(QStringLiteral("late_result"), workspace.path()), 5000);
        qputenv("BILLFISH_PYTHON", python);
        QVERIFY(started);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 5000);
        QCOMPARE(errors.first().at(0).toString(), QStringLiteral("media_transfer_start_failed"));
        QVERIFY(process.start(request(QStringLiteral("late_result"), workspace.path()), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        QCOMPARE(errors.count(), 1);
    }
};

QTEST_GUILESS_MAIN(MediaTransferTest)
#include "test_media_transfer_process.moc"
