#include "automation/video_capture.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

using billfish::automation::VideoCaptureController;

class VideoCaptureTest final : public QObject {
    Q_OBJECT
private slots:
    void confirmationSelectsActualChoiceAndLeasesDownload_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("providedTitle");
        QTest::addColumn<QString>("expectedTitle");
        QTest::newRow("site-title") << QStringLiteral("https://www.youtube.com/watch?v=example")
            << QStringLiteral("Page title") << QStringLiteral("Fixture video");
        QTest::newRow("generic-title") << QStringLiteral("https://example.test/master.m3u8")
            << QStringLiteral("光与色的练习") << QStringLiteral("光与色的练习");
    }

    void confirmationSelectsActualChoiceAndLeasesDownload()
    {
        QFETCH(QString, url);
        QFETCH(QString, providedTitle);
        QFETCH(QString, expectedTitle);
        QWidget main;
        VideoCaptureController capture;
        capture.setMainWindow(&main);
        QSignalSpy transfers(&capture, &VideoCaptureController::transferRequested);
        QSignalSpy imports(&capture, &VideoCaptureController::importRequested);
        QSignalSpy finished(&capture, &VideoCaptureController::finished);
        const QString id = QStringLiteral("confirmed");
        capture.start(id, {{QStringLiteral("url"), url}, {QStringLiteral("title"), providedTitle}},
                      QStringLiteral("C:/fixture-library"));
        QCOMPARE(transfers.count(), 1);
        const auto probe = transfers.at(0).at(1).toJsonObject();
        QCOMPARE(probe.value(QStringLiteral("mode")).toString(), QStringLiteral("probe_video"));
        const QString workspace = probe.value(QStringLiteral("workspace")).toString();
        capture.transferFinished(id, {{QStringLiteral("title"), QStringLiteral("Fixture video")},
            {QStringLiteral("choices"), QJsonArray{
                QJsonObject{{QStringLiteral("choice_id"), QStringLiteral("actual-1080")}, {QStringLiteral("label"), QStringLiteral("1080p")}},
                QJsonObject{{QStringLiteral("choice_id"), QStringLiteral("actual-720")}, {QStringLiteral("label"), QStringLiteral("720p")}}}}});
        // A successful probe is not authorization to download or import.
        QCOMPARE(transfers.count(), 1);
        QCOMPARE(imports.count(), 0);
        auto* dialog = main.findChild<QDialog*>(QStringLiteral("BillfishVideoCaptureDialog"));
        QVERIFY(dialog);
        auto* resolutions = dialog->findChild<QComboBox*>(QStringLiteral("videoCaptureResolution"));
        QCOMPARE(resolutions->count(), 2);
        resolutions->setCurrentIndex(1);
        dialog->findChild<QPushButton*>(QStringLiteral("videoCaptureDownload"))->click();
        QCOMPARE(transfers.count(), 2);
        const auto download = transfers.at(1).at(1).toJsonObject();
        QCOMPARE(download.value(QStringLiteral("mode")).toString(), QStringLiteral("download_video"));
        QCOMPARE(download.value(QStringLiteral("choice_id")).toString(), QStringLiteral("actual-720"));
        const QString output = workspace + QStringLiteral("/fixture.mp4");
        QFile video(output);
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write("downloaded-video-fixture");
        video.close();
        capture.transferFinished(id, {{QStringLiteral("path"), output},
                                     {QStringLiteral("title"), QStringLiteral("Fixture video")}});
        QCOMPARE(imports.count(), 1);
        QCOMPARE(imports.at(0).at(1).toString(), output);
        QCOMPARE(imports.at(0).at(2).toString(), expectedTitle);
        QCOMPARE(imports.at(0).at(4).toString(), QStringLiteral("C:/fixture-library"));
        QVERIFY(QFile::exists(output));
        QVERIFY(capture.has(id));
        capture.finishImport(id, {{QStringLiteral("source_safe_to_delete"), true}, {QStringLiteral("id"), 42}});
        QCOMPARE(finished.count(), 1);
        QVERIFY(!capture.has(id));
        QVERIFY(!QDir(workspace).exists());
    }

    void cancelBeforeConfirmationNeverDownloadsOrImports()
    {
        QWidget main;
        VideoCaptureController capture;
        capture.setMainWindow(&main);
        QSignalSpy transfers(&capture, &VideoCaptureController::transferRequested);
        QSignalSpy cancellations(&capture, &VideoCaptureController::transferCancelRequested);
        QSignalSpy imports(&capture, &VideoCaptureController::importRequested);
        QSignalSpy failed(&capture, &VideoCaptureController::failed);
        const QString id = QStringLiteral("cancelled");
        capture.start(id, {{QStringLiteral("url"), QStringLiteral("https://example.test/video.mp4")}},
                      QStringLiteral("C:/fixture-library"));
        const auto probe = transfers.at(0).at(1).toJsonObject();
        const QString workspace = probe.value(QStringLiteral("workspace")).toString();
        auto* dialog = main.findChild<QDialog*>(QStringLiteral("BillfishVideoCaptureDialog"));
        QVERIFY(dialog);
        dialog->findChild<QPushButton*>(QStringLiteral("videoCaptureCancel"))->click();
        QCOMPARE(cancellations.count(), 1);
        QVERIFY(capture.has(id));
        QVERIFY(QDir(workspace).exists());
        // Even a probe completion racing with cancel must not show a choice or download.
        capture.transferFinished(id, {{QStringLiteral("choices"), QJsonArray{
            QJsonObject{{QStringLiteral("choice_id"), QStringLiteral("original")}, {QStringLiteral("label"), QStringLiteral("原始分辨率")}}}}});
        QCOMPARE(transfers.count(), 1);
        QCOMPARE(imports.count(), 0);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("CANCELED"));
        QVERIFY(!capture.has(id));
        QVERIFY(!QDir(workspace).exists());
    }
};

QTEST_MAIN(VideoCaptureTest)
#include "test_video_capture.moc"
