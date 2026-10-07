#include "ui/mpv_process_backend.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCodec>
#include <QWidget>
#include <QtCore/qscopeguard.h>

#include <cstdlib>

using billfish::trim::MpvProcessBackend;

namespace {

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), previous_(qgetenv(name)), existed_(qEnvironmentVariableIsSet(name)) {
        qputenv(name, value);
    }

    ~ScopedEnvironment() {
        if (existed_) {
            qputenv(name_.constData(), previous_);
        } else {
            qunsetenv(name_.constData());
        }
    }

private:
    QByteArray name_;
    QByteArray previous_;
    bool existed_{false};
};

bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(data) == data.size();
}

QString fakeMpvPath() {
    return QString::fromUtf8(FAKE_MPV_PATH);
}

QString billfishRoot() {
    return QString::fromUtf8(BILLFISH_TEST_ROOT);
}

}  // namespace

class MpvProcessBackendTest final : public QObject {
    Q_OBJECT

private slots:
    void locatorHonorsPrecedence();
    void locatorPreservesUnicodeEnvironmentPath();
    void defaultArgumentsAreShellFreeAndHdrAware();
    void missingExecutableFailsSynchronously();
    void queuedProtocolRoundTripAndGracefulShutdown();
    void malformedProtocolReportsError();
    void unexpectedExitReportsErrorAndStops();
    void shutdownKillsAnUnresponsiveChild();
    void realPortableMpvSmoke();
};

void MpvProcessBackendTest::locatorHonorsPrecedence() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString environmentExe = temporary.filePath(QStringLiteral("env mpv.exe"));
    QVERIFY(QFile::copy(fakeMpvPath(), environmentExe));
    ScopedEnvironment explicitMpv("BILLFISH_TRIM_MPV", QFile::encodeName(environmentExe));
    QCOMPARE(QFileInfo(MpvProcessBackend::locateMpvExecutable(temporary.path())).canonicalFilePath(),
             QFileInfo(environmentExe).canonicalFilePath());

    qunsetenv("BILLFISH_TRIM_MPV");
    const QString bundledDir = temporary.filePath(
        QStringLiteral("_hdr-preview-patch/tools/mpv"));
    QVERIFY(QDir().mkpath(bundledDir));
    const QString bundledExe = QDir(bundledDir).filePath(QStringLiteral("mpv.exe"));
    QVERIFY(QFile::copy(fakeMpvPath(), bundledExe));
    QCOMPARE(QFileInfo(MpvProcessBackend::locateMpvExecutable(temporary.path())).canonicalFilePath(),
             QFileInfo(bundledExe).canonicalFilePath());
}

void MpvProcessBackendTest::locatorPreservesUnicodeEnvironmentPath() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString environmentExe = temporary.filePath(QStringLiteral("中文 mpv.exe"));
    QVERIFY(QFile::copy(fakeMpvPath(), environmentExe));

    const QString previous = qEnvironmentVariable("BILLFISH_TRIM_MPV");
    const auto restoreEnvironment = qScopeGuard([previous] {
        _wputenv_s(L"BILLFISH_TRIM_MPV",
                   reinterpret_cast<const wchar_t*>(previous.utf16()));
    });
    QCOMPARE(_wputenv_s(L"BILLFISH_TRIM_MPV",
                       reinterpret_cast<const wchar_t*>(environmentExe.utf16())), 0);

    QTextCodec* previousCodec = QTextCodec::codecForLocale();
    const auto restoreCodec = qScopeGuard([previousCodec] {
        QTextCodec::setCodecForLocale(previousCodec);
    });
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("Windows-1252"));
    QCOMPARE(QFileInfo(MpvProcessBackend::locateMpvExecutable(temporary.path())).canonicalFilePath(),
             QFileInfo(environmentExe).canonicalFilePath());
}

void MpvProcessBackendTest::defaultArgumentsAreShellFreeAndHdrAware() {
    const QString pipe = QStringLiteral("\\\\.\\pipe\\backend-test");
    const QStringList arguments = MpvProcessBackend::defaultArguments(0x1234, pipe);
    QVERIFY(arguments.contains(QStringLiteral("--no-config")));
    QVERIFY(arguments.contains(QStringLiteral("--wid=4660")));
    QVERIFY(arguments.contains(QStringLiteral("--input-ipc-server=%1").arg(pipe)));
    QVERIFY(arguments.contains(QStringLiteral("--vo=gpu-next")));
    QVERIFY(arguments.contains(QStringLiteral("--gpu-api=d3d11")));
    QVERIFY(arguments.contains(QStringLiteral("--tone-mapping=bt.2446a")));
    QVERIFY(arguments.contains(QStringLiteral("--gamut-mapping-mode=perceptual")));
    for (const QString& argument : arguments) {
        QVERIFY2(!argument.contains(QLatin1Char('"')), qPrintable(argument));
        QVERIFY2(!argument.contains(QStringLiteral("cmd.exe"), Qt::CaseInsensitive),
                 qPrintable(argument));
    }
}

void MpvProcessBackendTest::missingExecutableFailsSynchronously() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", "Z:\\missing\\mpv.exe");
    ScopedEnvironment path("PATH", QFile::encodeName(temporary.path()));
    QWidget host;
    host.show();
    MpvProcessBackend backend(&host);
    QSignalSpy errorSpy(&backend, &MpvProcessBackend::errorOccurred);
    QVERIFY(!backend.start(temporary.path()));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(backend.lastError().contains(QStringLiteral("未找到 mpv")));
    QVERIFY(!backend.isRunning());
}

void MpvProcessBackendTest::queuedProtocolRoundTripAndGracefulShutdown() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString logPath = temporary.filePath(QStringLiteral("fake-mpv.log"));
    const QString mediaPath = temporary.filePath(QStringLiteral("片段 & source.mp4"));
    QVERIFY(writeFile(mediaPath, "fake media"));
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", QFile::encodeName(fakeMpvPath()));
    ScopedEnvironment log("BILLFISH_FAKE_MPV_LOG", QFile::encodeName(logPath));
    ScopedEnvironment mode("BILLFISH_FAKE_MPV_MODE", "normal");

    QWidget host;
    host.resize(640, 360);
    host.show();
    QCoreApplication::processEvents();
    MpvProcessBackend backend(&host);
    QSignalSpy readySpy(&backend, &MpvProcessBackend::ready);
    QSignalSpy loadedSpy(&backend, &MpvProcessBackend::fileLoaded);
    QSignalSpy positionSpy(&backend, &MpvProcessBackend::positionChanged);
    QSignalSpy durationSpy(&backend, &MpvProcessBackend::durationChanged);
    QSignalSpy pauseSpy(&backend, &MpvProcessBackend::pausedChanged);
    QSignalSpy errorSpy(&backend, &MpvProcessBackend::errorOccurred);
    QSignalSpy stoppedSpy(&backend, &MpvProcessBackend::stopped);

    QVERIFY(backend.start(temporary.path()));
    QVERIFY(backend.load(mediaPath));
    QVERIFY(backend.setAbLoop(1'250, 8'750));
    QVERIFY(backend.seek(4'321));
    QVERIFY(backend.play());
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), 1, 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(backend.durationMs(), qint64{12'345}, 5'000);
    QTRY_COMPARE_WITH_TIMEOUT(backend.positionMs(), qint64{4'321}, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isPaused(), 5'000);
    QVERIFY(positionSpy.count() >= 1);
    QVERIFY(durationSpy.count() >= 1);
    QVERIFY(pauseSpy.count() >= 1);

    QVERIFY(backend.pause());
    QTRY_VERIFY_WITH_TIMEOUT(backend.isPaused(), 5'000);
    QVERIFY(backend.clearAbLoop());
    backend.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(stoppedSpy.count(), 1, 5'000);
    QVERIFY(!backend.isRunning());
    QCOMPARE(errorSpy.count(), 0);

    QFile logFile(logPath);
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    const QByteArray transcript = logFile.readAll();
    QVERIFY(transcript.contains("--no-config"));
    QVERIFY(transcript.contains("--vo=gpu-next"));
    QVERIFY(transcript.contains("loadfile"));
    QVERIFY(transcript.contains(mediaPath.toUtf8()));
    QVERIFY(transcript.contains("ab-loop-a"));
    QVERIFY(transcript.contains("ab-loop-b"));
    QVERIFY(transcript.contains("absolute+exact"));
    QVERIFY(transcript.contains("quit"));
}

void MpvProcessBackendTest::malformedProtocolReportsError() {
    QTemporaryDir temporary;
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", QFile::encodeName(fakeMpvPath()));
    ScopedEnvironment mode("BILLFISH_FAKE_MPV_MODE", "malformed");
    QWidget host;
    host.show();
    MpvProcessBackend backend(&host);
    QSignalSpy errorSpy(&backend, &MpvProcessBackend::errorOccurred);
    QVERIFY(backend.start(temporary.path()));
    QTRY_VERIFY_WITH_TIMEOUT(backend.isReady(), 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 5'000);
    QVERIFY(backend.lastError().contains(QStringLiteral("无效 JSON")));
    backend.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isRunning(), 5'000);
}

void MpvProcessBackendTest::unexpectedExitReportsErrorAndStops() {
    QTemporaryDir temporary;
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", QFile::encodeName(fakeMpvPath()));
    ScopedEnvironment mode("BILLFISH_FAKE_MPV_MODE", "exit-after-connect");
    QWidget host;
    host.show();
    MpvProcessBackend backend(&host);
    QSignalSpy errorSpy(&backend, &MpvProcessBackend::errorOccurred);
    QSignalSpy stoppedSpy(&backend, &MpvProcessBackend::stopped);
    QVERIFY(backend.start(temporary.path()));
    QTRY_COMPARE_WITH_TIMEOUT(stoppedSpy.count(), 1, 5'000);
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 5'000);
    QTest::qWait(100);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!backend.isRunning());
}

void MpvProcessBackendTest::shutdownKillsAnUnresponsiveChild() {
    QTemporaryDir temporary;
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", QFile::encodeName(fakeMpvPath()));
    ScopedEnvironment mode("BILLFISH_FAKE_MPV_MODE", "ignore-quit");
    QWidget host;
    host.show();
    MpvProcessBackend backend(&host);
    QSignalSpy stoppedSpy(&backend, &MpvProcessBackend::stopped);
    QVERIFY(backend.start(temporary.path()));
    QTRY_VERIFY_WITH_TIMEOUT(backend.isReady(), 5'000);
    backend.shutdown();
    const int maximumWait = MpvProcessBackend::kGracefulShutdownTimeoutMs +
                            MpvProcessBackend::kTerminateTimeoutMs + 3'000;
    QTRY_COMPARE_WITH_TIMEOUT(stoppedSpy.count(), 1, maximumWait);
    QVERIFY(!backend.isRunning());
}

void MpvProcessBackendTest::realPortableMpvSmoke() {
    if (qgetenv("BILLFISH_RUN_REAL_MPV_SMOKE") != QByteArrayLiteral("1")) {
        QSKIP("Set BILLFISH_RUN_REAL_MPV_SMOKE=1 to run the local portable-mpv smoke test");
    }
    const QString executable = QDir(billfishRoot()).filePath(
        QStringLiteral("_hdr-preview-patch/tools/mpv/mpv.exe"));
    QStringList mediaPaths{
        QDir(billfishRoot()).filePath(QStringLiteral(
            "task-memory/task-billfish-layout-patch/artifacts/media-sdk-zip-extract/codec/h264_4K.mp4")),
        QDir(billfishRoot()).filePath(QStringLiteral(
            "task-memory/task-billfish-layout-patch/artifacts/media-sdk-zip-extract/codec/h265_4K.mp4"))};
    const QString av1Media = QString::fromLocal8Bit(qgetenv("BILLFISH_MPV_AV1_SMOKE_MEDIA"));
    const QString hdrMedia = QString::fromLocal8Bit(qgetenv("BILLFISH_MPV_HDR_SMOKE_MEDIA"));
    if (!av1Media.isEmpty()) {
        mediaPaths.append(av1Media);
    }
    if (!hdrMedia.isEmpty()) {
        mediaPaths.append(hdrMedia);
    }
    QVERIFY2(QFileInfo(executable).isFile(), qPrintable(executable));
    for (const QString& media : mediaPaths) {
        QVERIFY2(QFileInfo(media).isFile(), qPrintable(media));
    }
    ScopedEnvironment mpv("BILLFISH_TRIM_MPV", QFile::encodeName(executable));
    ScopedEnvironment fakeMode("BILLFISH_FAKE_MPV_MODE", QByteArray());

    QWidget host;
    host.resize(640, 360);
    host.show();
    QCoreApplication::processEvents();
    MpvProcessBackend backend(&host);
    QSignalSpy loadedSpy(&backend, &MpvProcessBackend::fileLoaded);
    QSignalSpy errorSpy(&backend, &MpvProcessBackend::errorOccurred);
    QVERIFY(backend.start(billfishRoot()));
    QTRY_VERIFY_WITH_TIMEOUT(backend.isReady(), 10'000);
    for (int index = 0; index < mediaPaths.size(); ++index) {
        QVERIFY(backend.load(mediaPaths.at(index)));
        QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), index + 1, 15'000);
    }
    QVERIFY(backend.setAbLoop(0, 1'000));
    QVERIFY(backend.seek(0));
    QVERIFY(backend.play());
    QVERIFY(backend.pause());
    QVERIFY(backend.clearAbLoop());
    QTest::qWait(100);
    backend.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isRunning(), 5'000);
    QCOMPARE(errorSpy.count(), 0);
}

QTEST_MAIN(MpvProcessBackendTest)

#include "test_mpv_process_backend.moc"
