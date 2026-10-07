#include "video_trim_core.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLibrary>
#include <QtCore/QProcess>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtCore/QThread>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlobj_core.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <utility>

using namespace billfish::trim;

namespace {

class TestRunner final {
public:
    void check(bool condition, const QString &message)
    {
        ++checks_;
        if (condition) {
            return;
        }
        ++failures_;
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    }

    template<typename Actual, typename Expected>
    void equal(const Actual &actual, const Expected &expected, const QString &message)
    {
        check(actual == expected, message);
    }

    [[nodiscard]] int finish() const
    {
        QTextStream stream(stdout);
        stream << "Core checks: " << checks_ << ", failures: " << failures_ << Qt::endl;
        return failures_ == 0 ? 0 : 1;
    }

private:
    int checks_ = 0;
    int failures_ = 0;
};

bool writeFile(const QString &path, const QByteArray &content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(content) == content.size();
}

class ScopedEnvironmentVariable final {
public:
    explicit ScopedEnvironmentVariable(QByteArray name)
        : name_(std::move(name)), wasSet_(qEnvironmentVariableIsSet(name_.constData())), value_(qgetenv(name_.constData()))
    {
    }

    ~ScopedEnvironmentVariable()
    {
        if (wasSet_) {
            qputenv(name_.constData(), value_);
        } else {
            qunsetenv(name_.constData());
        }
    }

    void set(const QByteArray &value) const
    {
        qputenv(name_.constData(), value);
    }

    void unset() const
    {
        qunsetenv(name_.constData());
    }

private:
    QByteArray name_;
    bool wasSet_ = false;
    QByteArray value_;
};

bool createFakeToolPair(const QString &directory, FfmpegTools *tools, QString *error)
{
    if (!QDir().mkpath(directory)) {
        *error = QStringLiteral("could not create fake FFmpeg directory");
        return false;
    }
#ifdef Q_OS_WIN
    const QString ffmpegName = QStringLiteral("ffmpeg.exe");
    const QString ffprobeName = QStringLiteral("ffprobe.exe");
#else
    const QString ffmpegName = QStringLiteral("ffmpeg");
    const QString ffprobeName = QStringLiteral("ffprobe");
#endif
    const QString source = QCoreApplication::applicationFilePath();
    const QString ffmpeg = QDir(directory).filePath(ffmpegName);
    const QString ffprobe = QDir(directory).filePath(ffprobeName);
    if (!QFile::copy(source, ffmpeg) || !QFile::copy(source, ffprobe)) {
        *error = QStringLiteral("could not copy the core test executable as fake FFmpeg tools");
        return false;
    }
#ifdef Q_OS_WIN
    const QString qtCoreSource = QDir(QCoreApplication::applicationDirPath())
                                     .filePath(QStringLiteral("Qt5Core.dll"));
    const QString qtCoreTarget = QDir(directory).filePath(QStringLiteral("Qt5Core.dll"));
    if (QFileInfo::exists(qtCoreSource) && !QFile::copy(qtCoreSource, qtCoreTarget)) {
        *error = QStringLiteral("could not copy Qt5Core.dll for the fake FFmpeg tools");
        return false;
    }
#endif
    if (!writeFile(
            QDir(directory).filePath(QStringLiteral("billfish-fake-ffmpeg.marker")),
            "fake-tool-contract-v1\n")) {
        *error = QStringLiteral("could not write the fake FFmpeg contract marker");
        return false;
    }
    tools->ffmpegPath = QFileInfo(ffmpeg).absoluteFilePath();
    tools->ffprobePath = QFileInfo(ffprobe).absoluteFilePath();
    return true;
}

void recordCapabilityInvocation(const QStringList &arguments)
{
    const QString counterPath = QString::fromLocal8Bit(
        qgetenv("BILLFISH_TEST_CAPABILITY_COUNTER"));
    if (counterPath.isEmpty()) {
        return;
    }
    QFile counter(counterPath);
    if (!counter.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    counter.write(QFileInfo(QCoreApplication::applicationFilePath()).fileName().toUtf8());
    counter.write(" ");
    counter.write(arguments.join(QLatin1Char(' ')).toUtf8());
    counter.write("\n");
}

int fakeToolContract(const QStringList &arguments)
{
    recordCapabilityInvocation(arguments);
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    const bool ffprobe = executable.baseName().contains(
        QStringLiteral("ffprobe"), Qt::CaseInsensitive);
    const bool invalidContract = QFileInfo::exists(
        QDir(executable.absolutePath()).filePath(QStringLiteral("invalid-contract.marker")));
    if (arguments.contains(QStringLiteral("-version"))) {
        const QByteArray version = invalidContract
            ? QByteArray("Billfish non-standard wrapper test double\n")
            : (ffprobe
                   ? QByteArray("ffprobe version billfish-test-1\n")
                   : QByteArray("ffmpeg version billfish-test-1\n"));
        std::fwrite(version.constData(), 1, static_cast<size_t>(version.size()), stdout);
        std::fflush(stdout);
        return 0;
    }
    if (arguments.contains(QStringLiteral("encoder=libx264"))) {
        if (qEnvironmentVariableIsSet("BILLFISH_TEST_NO_LIBX264")) {
            std::fputs("Codec 'libx264' is not recognized by FFmpeg.\n", stdout);
        } else {
            std::fputs("Encoder libx264 [test encoder]:\n", stdout);
            if (!qEnvironmentVariableIsSet("BILLFISH_TEST_NO_CRF")) {
                std::fputs("  -crf <float> E..V....... constant quality\n", stdout);
            }
        }
        std::fflush(stdout);
        return 0;
    }
    return -1;
}

qint64 parseTimestamp(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 3) {
        return -1;
    }
    bool hoursOk = false;
    bool minutesOk = false;
    bool secondsOk = false;
    const qint64 hours = parts.at(0).toLongLong(&hoursOk);
    const qint64 minutes = parts.at(1).toLongLong(&minutesOk);
    const double seconds = parts.at(2).toDouble(&secondsOk);
    if (!hoursOk || !minutesOk || !secondsOk) {
        return -1;
    }
    return hours * 3'600'000 + minutes * 60'000
        + static_cast<qint64>(std::llround(seconds * 1'000.0));
}

int fakeFfprobe(const QStringList &arguments)
{
    const QString path = arguments.constLast();
    qint64 durationMs = 10'000;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        const QByteArray marker("durationMs=");
        const int markerIndex = data.indexOf(marker);
        if (markerIndex >= 0) {
            const int valueStart = markerIndex + marker.size();
            const int valueEnd = data.indexOf('\n', valueStart);
            bool ok = false;
            const qint64 parsed = data.mid(
                valueStart,
                valueEnd < 0 ? -1 : valueEnd - valueStart).toLongLong(&ok);
            if (ok) {
                durationMs = parsed;
            }
        }
    }

    const QString seconds = QString::number(durationMs / 1'000.0, 'f', 3);
    const QJsonObject root{
        {QStringLiteral("format"),
         QJsonObject{
             {QStringLiteral("duration"), seconds},
             {QStringLiteral("format_name"), QStringLiteral("mov,mp4")},
             {QStringLiteral("start_time"), QStringLiteral("0.000")},
         }},
        {QStringLiteral("streams"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("index"), 0},
                 {QStringLiteral("codec_type"), QStringLiteral("video")},
                 {QStringLiteral("codec_name"), QStringLiteral("h264")},
                 {QStringLiteral("width"), 1920},
                 {QStringLiteral("height"), 1080},
                 {QStringLiteral("avg_frame_rate"), QStringLiteral("24000/1001")},
                 {QStringLiteral("duration"), seconds},
             },
             QJsonObject{
                 {QStringLiteral("index"), 1},
                 {QStringLiteral("codec_type"), QStringLiteral("audio")},
                 {QStringLiteral("codec_name"), QStringLiteral("aac")},
                 {QStringLiteral("duration"), seconds},
             },
         }},
    };
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    std::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()), stdout);
    std::fflush(stdout);
    return 0;
}

int fakeFfmpeg(const QStringList &arguments)
{
    if (qEnvironmentVariableIsSet("BILLFISH_TEST_FAIL_FFMPEG")) {
        std::fputs("intentional ffmpeg failure\n", stderr);
        return 8;
    }
    const int durationIndex = arguments.indexOf(QStringLiteral("-t"));
    qint64 durationMs = durationIndex >= 0 && durationIndex + 1 < arguments.size()
        ? parseTimestamp(arguments.at(durationIndex + 1))
        : -1;
    bool overrideOk = false;
    const qint64 overrideDuration = qEnvironmentVariableIntValue(
        "BILLFISH_TEST_OUTPUT_DURATION_MS",
        &overrideOk);
    if (overrideOk) {
        durationMs = overrideDuration;
    }
    const QByteArray halfProgress = QByteArray("out_time_us=")
        + QByteArray::number(std::max<qint64>(0, durationMs / 2) * 1'000)
        + "\nprogress=continue\n";
    const QByteArray finalProgress = QByteArray("out_time_ms=")
        + QByteArray::number(std::max<qint64>(0, durationMs) * 1'000)
        + "\nprogress=end\n";
    std::fwrite(
        halfProgress.constData(),
        1,
        static_cast<size_t>(halfProgress.size()),
        stdout);
    std::fwrite(
        finalProgress.constData(),
        1,
        static_cast<size_t>(finalProgress.size()),
        stdout);
    std::fflush(stdout);
    const QString outputPath = arguments.constLast();
    QByteArray sourceMarker;
    if (qEnvironmentVariableIsSet("BILLFISH_TEST_COPY_INPUT_MARKER")) {
        const int inputIndex = arguments.indexOf(QStringLiteral("-i"));
        if (inputIndex >= 0 && inputIndex + 1 < arguments.size()) {
            QFile inputFile(arguments.at(inputIndex + 1));
            if (inputFile.open(QIODevice::ReadOnly)) {
                const QByteArray input = inputFile.readAll();
                const QByteArray marker("source=");
                const int markerIndex = input.indexOf(marker);
                if (markerIndex >= 0) {
                    const int valueStart = markerIndex + marker.size();
                    const int valueEnd = input.indexOf('\n', valueStart);
                    sourceMarker = input.mid(valueStart,
                                             valueEnd < 0 ? -1 : valueEnd - valueStart).trimmed();
                }
            }
        }
    }
    const QByteArray markerSuffix = sourceMarker.isEmpty()
        ? QByteArray()
        : QByteArray("\nsource=") + sourceMarker;
    return writeFile(
               outputPath,
               QByteArray("fake-media\ndurationMs=") + QByteArray::number(durationMs) + markerSuffix + '\n')
        ? 0
        : 9;
}

int runChildMode(const QStringList &arguments)
{
    const bool fakeToolContractEnabled = QFileInfo::exists(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("billfish-fake-ffmpeg.marker")));
    if (fakeToolContractEnabled
        && (arguments.contains(QStringLiteral("-version"))
            || arguments.contains(QStringLiteral("encoder=libx264")))) {
        return fakeToolContract(arguments);
    }
    if (fakeToolContractEnabled && arguments.contains(QStringLiteral("-show_entries"))) {
        return fakeFfprobe(arguments);
    }
    if (fakeToolContractEnabled && arguments.contains(QStringLiteral("-hide_banner"))) {
        return fakeFfmpeg(arguments);
    }
    if (arguments.value(1) == QStringLiteral("--child-echo")) {
        const QByteArray text = arguments.value(2).toUtf8();
        std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stdout);
        return 0;
    }
    if (arguments.value(1) == QStringLiteral("--child-exit-7")) {
        std::fputs("child failed as requested", stderr);
        return 7;
    }
    if (arguments.value(1) == QStringLiteral("--child-sleep")) {
        QThread::msleep(1'000);
        return 0;
    }
    return -1;
}

void testRangeSelection(TestRunner &test)
{
    const RangeSelection selection{1'234, 65'432};
    test.check(selection.isValid(), QStringLiteral("valid range"));
    test.equal(selection.durationMs(), qint64{64'198}, QStringLiteral("range duration"));
    test.equal(
        RangeSelection{9, 2}.normalized(),
        RangeSelection{2, 9},
        QStringLiteral("range normalization"));
    test.equal(
        RangeSelection{-5, 12}.clamped(10),
        RangeSelection{0, 10},
        QStringLiteral("range clamping"));
    test.check(
        !RangeSelection{3, 3}.isValid() && !RangeSelection{-1, 2}.validationError().isEmpty(),
        QStringLiteral("invalid ranges rejected"));
    test.equal(
        formatTimestampMs(3'661'007),
        QStringLiteral("01:01:01.007"),
        QStringLiteral("ffmpeg timestamp formatting"));
    test.equal(
        formatTimestampMs(-1),
        QStringLiteral("-00:00:00.001"),
        QStringLiteral("negative timestamp formatting"));
}

void testToolDiscoveryAndArguments(TestRunner &test)
{
    QTemporaryDir temporary;
    test.check(temporary.isValid(), QStringLiteral("tool discovery temp dir"));
    ScopedEnvironmentVariable pathEnvironment("PATH");
    ScopedEnvironmentVariable ffmpegEnvironment("BILLFISH_FFMPEG");
    ScopedEnvironmentVariable ffprobeEnvironment("BILLFISH_FFPROBE");
    ScopedEnvironmentVariable homeEnvironment("FFMPEG_HOME");
    ScopedEnvironmentVariable counterEnvironment("BILLFISH_TEST_CAPABILITY_COUNTER");
    ScopedEnvironmentVariable noLibx264Environment("BILLFISH_TEST_NO_LIBX264");
    ScopedEnvironmentVariable noCrfEnvironment("BILLFISH_TEST_NO_CRF");
    ffmpegEnvironment.unset();
    ffprobeEnvironment.unset();
    homeEnvironment.unset();
    counterEnvironment.unset();
    noLibx264Environment.unset();
    noCrfEnvironment.unset();

    QString fixtureError;
    FfmpegTools supportedFixture;
    FfmpegTools environmentFixture;
    FfmpegTools homeFixture;
    FfmpegTools pathFixture;
    const QString supportedDirectory = QDir(temporary.path()).filePath(QStringLiteral("supported"));
    const QString environmentDirectory = QDir(temporary.path()).filePath(QStringLiteral("environment"));
    const QString homeDirectory = QDir(temporary.path()).filePath(QStringLiteral("home/bin"));
    const QString pathDirectory = QDir(temporary.path()).filePath(QStringLiteral("path"));
    const bool fixturesCreated = createFakeToolPair(
        supportedDirectory, &supportedFixture, &fixtureError)
        && createFakeToolPair(environmentDirectory, &environmentFixture, &fixtureError)
        && createFakeToolPair(homeDirectory, &homeFixture, &fixtureError)
        && createFakeToolPair(pathDirectory, &pathFixture, &fixtureError);
    test.check(fixturesCreated, QStringLiteral("fake FFmpeg pair fixtures: %1").arg(fixtureError));
    if (!fixturesCreated) {
        return;
    }

    const QString wrapperDirectory = QDir(temporary.path()).filePath(QStringLiteral("application"));
    FfmpegTools wrapperFixture;
    test.check(
        createFakeToolPair(wrapperDirectory, &wrapperFixture, &fixtureError),
        QStringLiteral("non-standard wrapper PE fixture: %1").arg(fixtureError));
    const QString fakeFfmpeg = wrapperFixture.ffmpegPath;
    const QString ffmpegLite = QDir(wrapperDirectory).filePath(QStringLiteral("ffmpegLite64.exe"));
    test.check(
        writeFile(
            QDir(wrapperDirectory).filePath(QStringLiteral("invalid-contract.marker")),
            "invalid")
            && QFile::copy(QCoreApplication::applicationFilePath(), ffmpegLite),
        QStringLiteral("non-standard wrapper fixtures"));

    pathEnvironment.set(QByteArray());
    const FfmpegTools rejectedWrappers = locateFfmpegTools(wrapperDirectory);
    test.check(!rejectedWrappers.isComplete(), QStringLiteral("non-standard wrappers rejected"));
    test.check(
        rejectedWrappers.ffmpegPath.compare(ffmpegLite, Qt::CaseInsensitive) != 0,
        QStringLiteral("ffmpegLite64 is never selected as full FFmpeg"));
    test.check(
        rejectedWrappers.discoveryError.contains(
            QDir::toNativeSeparators(QFileInfo(fakeFfmpeg).absoluteFilePath()),
            Qt::CaseInsensitive),
        QStringLiteral("wrapper rejection identifies actual executable"));
    FfmpegTools liteTools;
    liteTools.ffmpegPath = ffmpegLite;
    liteTools.ffprobePath = supportedFixture.ffprobePath;
    test.check(
        !liteTools.canTrim()
            && liteTools.trimCapabilityError(false).contains(
                QStringLiteral("ffmpegLite64"), Qt::CaseInsensitive),
        QStringLiteral("explicit ffmpegLite64 path is rejected without execution"));

    const FfmpegTools supported = locateFfmpegTools(
        wrapperDirectory, {}, {}, {supportedDirectory});
    test.equal(
        supported.ffmpegPath,
        supportedFixture.ffmpegPath,
        QStringLiteral("supported directory fallback skips wrappers"));
    test.equal(
        supported.ffprobePath,
        supportedFixture.ffprobePath,
        QStringLiteral("supported directory returns paired FFprobe"));
    test.check(supported.isComplete(), QStringLiteral("validated supported tool pair"));

    ffmpegEnvironment.set(environmentFixture.ffmpegPath.toLocal8Bit());
    ffprobeEnvironment.set(environmentFixture.ffprobePath.toLocal8Bit());
    homeEnvironment.set(QFileInfo(homeDirectory).absolutePath().toLocal8Bit());
    pathEnvironment.set(pathDirectory.toLocal8Bit());
    const FfmpegTools environmentSelected = locateFfmpegTools(
        wrapperDirectory, {}, {}, {supportedDirectory});
    test.equal(
        environmentSelected.ffmpegPath,
        environmentFixture.ffmpegPath,
        QStringLiteral("BILLFISH_FFMPEG pair has discovery priority"));
    test.equal(
        environmentSelected.ffprobePath,
        environmentFixture.ffprobePath,
        QStringLiteral("BILLFISH_FFPROBE remains paired"));

    ffmpegEnvironment.unset();
    ffprobeEnvironment.unset();
    const FfmpegTools homeSelected = locateFfmpegTools(
        wrapperDirectory, {}, {}, {supportedDirectory});
    test.equal(
        homeSelected.ffmpegPath,
        homeFixture.ffmpegPath,
        QStringLiteral("FFMPEG_HOME precedes PATH"));
    test.equal(
        homeSelected.ffprobePath,
        homeFixture.ffprobePath,
        QStringLiteral("FFMPEG_HOME tools are paired"));

    homeEnvironment.unset();
    const FfmpegTools pathSelected = locateFfmpegTools(
        wrapperDirectory, {}, {}, {supportedDirectory});
    test.equal(
        pathSelected.ffmpegPath,
        pathFixture.ffmpegPath,
        QStringLiteral("PATH pair precedes supported directories"));
    test.equal(
        QFileInfo(pathSelected.ffmpegPath).absolutePath(),
        QFileInfo(pathSelected.ffprobePath).absolutePath(),
        QStringLiteral("PATH FFprobe must come from FFmpeg directory"));

    const FfmpegTools explicitSelected = locateFfmpegTools(
        wrapperDirectory,
        supportedFixture.ffmpegPath,
        supportedFixture.ffprobePath,
        {environmentDirectory});
    test.equal(
        explicitSelected.ffmpegPath,
        supportedFixture.ffmpegPath,
        QStringLiteral("explicit API paths override automatic discovery"));

    FfmpegTools cachedFixture;
    const QString cachedDirectory = QDir(temporary.path()).filePath(QStringLiteral("cached"));
    test.check(
        createFakeToolPair(cachedDirectory, &cachedFixture, &fixtureError),
        QStringLiteral("capability cache fixture: %1").arg(fixtureError));
    const QString counterPath = QDir(temporary.path()).filePath(QStringLiteral("contract-count.txt"));
    counterEnvironment.set(counterPath.toLocal8Bit());
    test.check(
        cachedFixture.canTrim() && cachedFixture.canProbe() && cachedFixture.canAccurateTrim(),
        QStringLiteral("fake tools satisfy full capability contract"));
    test.check(
        cachedFixture.canTrim() && cachedFixture.canProbe() && cachedFixture.canAccurateTrim(),
        QStringLiteral("capability contract repeat remains valid"));
    counterEnvironment.unset();
    QFile counter(counterPath);
    test.check(counter.open(QIODevice::ReadOnly), QStringLiteral("capability counter readable"));
    const int capabilityInvocations = counter.readAll().split('\n').size() - 1;
    test.equal(capabilityInvocations, 3, QStringLiteral("capability contract cached per executable signature"));

    FfmpegTools noCrfFixture;
    const QString noCrfDirectory = QDir(temporary.path()).filePath(QStringLiteral("no-crf"));
    test.check(
        createFakeToolPair(noCrfDirectory, &noCrfFixture, &fixtureError),
        QStringLiteral("no-CRF fixture: %1").arg(fixtureError));
    noCrfEnvironment.set("1");
    test.check(noCrfFixture.canTrim(), QStringLiteral("standard FFmpeg remains usable for fast trim"));
    test.check(!noCrfFixture.canAccurateTrim(), QStringLiteral("accurate trim requires CRF capability"));
    noCrfEnvironment.unset();
    const QString noCrfError = noCrfFixture.trimCapabilityError(true);
    test.check(
        noCrfError.contains(QStringLiteral("-crf"))
            && noCrfError.contains(
                QDir::toNativeSeparators(noCrfFixture.ffmpegPath), Qt::CaseInsensitive),
        QStringLiteral("accurate capability error names option and executable"));

    const QString input = QDir(temporary.path()).filePath(QStringLiteral("input with spaces.mp4"));
    const QString output = QDir(temporary.path()).filePath(QStringLiteral("output with spaces.mp4"));
    test.check(writeFile(input, "input"), QStringLiteral("argument input"));

    TrimRequest unsupportedAccurate;
    unsupportedAccurate.inputPath = input;
    unsupportedAccurate.outputPath = QDir(temporary.path()).filePath(QStringLiteral("unsupported.mp4"));
    unsupportedAccurate.range = {0, 1'000};
    const TrimResult unsupportedAccurateResult = runTrim(noCrfFixture, unsupportedAccurate);
    test.check(
        !unsupportedAccurateResult.success
            && unsupportedAccurateResult.process.termination == ProcessTermination::NotStarted,
        QStringLiteral("accurate capability rejected before launching FFmpeg"));
    test.check(
        unsupportedAccurateResult.error.contains(QStringLiteral("-crf"))
            && unsupportedAccurateResult.error.contains(
                QDir::toNativeSeparators(noCrfFixture.ffmpegPath), Qt::CaseInsensitive),
        QStringLiteral("accurate gate error identifies executable"));

    TrimRequest request;
    request.inputPath = input;
    request.outputPath = output;
    request.range = {1'250, 4'000};
    request.extraOutputArguments = QStringList{
        QStringLiteral("-metadata"),
        QStringLiteral("title=A B")};
    const QStringList accurate = buildTrimArguments(request);
    const int accurateInput = accurate.indexOf(QStringLiteral("-i"));
    const int accurateSeek = accurate.indexOf(QStringLiteral("-ss"));
    test.check(accurateInput >= 0 && accurateSeek > accurateInput, QStringLiteral("accurate seek after input"));
    test.equal(accurate.value(accurateSeek + 1), QStringLiteral("00:00:01.250"), QStringLiteral("accurate seek value"));
    test.equal(accurate.value(accurate.indexOf(QStringLiteral("-t")) + 1), QStringLiteral("00:00:02.750"), QStringLiteral("duration argv"));
    test.equal(accurate.constLast(), output, QStringLiteral("argv preserves spaced output as one item"));
    test.check(accurate.contains(QStringLiteral("+faststart")), QStringLiteral("MP4 faststart"));
    const int progressIndex = accurate.indexOf(QStringLiteral("-progress"));
    test.check(
        progressIndex >= 0 && accurate.value(progressIndex + 1) == QStringLiteral("pipe:1")
            && accurate.contains(QStringLiteral("-nostats")),
        QStringLiteral("ffmpeg machine progress argv"));

    request.mode = TrimMode::FastCopy;
    const QStringList fast = buildTrimArguments(request);
    test.check(
        fast.indexOf(QStringLiteral("-ss")) < fast.indexOf(QStringLiteral("-i")),
        QStringLiteral("fast seek before input"));
    test.check(fast.contains(QStringLiteral("copy")), QStringLiteral("fast stream copy"));
    test.equal(
        buildProbeArguments(input).constLast(),
        input,
        QStringLiteral("probe argv preserves input path"));
}

void testProcessExecution(TestRunner &test)
{
    ProcessSpec spec;
    spec.program = QCoreApplication::applicationFilePath();
    spec.arguments = QStringList{
        QStringLiteral("--child-echo"),
        QStringLiteral("hello process")};
    spec.timeoutMs = 5'000;
    ProcessResult result = runProcess(spec);
    test.check(result.succeeded(), QStringLiteral("child process succeeds"));
    test.equal(result.standardOutput, QByteArray("hello process"), QStringLiteral("stdout captured"));
    QByteArray streamedOutput;
    result = runProcess(
        spec,
        {},
        [&](const QByteArray &standardOutput, const QByteArray &) {
            streamedOutput += standardOutput;
        });
    test.check(result.succeeded(), QStringLiteral("streaming process succeeds"));
    test.equal(streamedOutput, QByteArray("hello process"), QStringLiteral("streaming output callback"));

    spec.arguments = QStringList{QStringLiteral("--child-exit-7")};
    result = runProcess(spec);
    test.check(!result.succeeded() && result.exitCode == 7, QStringLiteral("nonzero exit captured"));
    test.check(result.error.contains(QStringLiteral("child failed")), QStringLiteral("stderr promoted to error"));

    spec.arguments = QStringList{QStringLiteral("--child-sleep")};
    spec.timeoutMs = 100;
    result = runProcess(spec);
    test.equal(result.termination, ProcessTermination::TimedOut, QStringLiteral("process timeout"));
    test.check(result.elapsedMs < 2'000, QStringLiteral("timed-out process killed promptly"));

    bool cancel = true;
    spec.timeoutMs = 5'000;
    result = runProcess(spec, [&cancel] { return cancel; });
    test.equal(result.termination, ProcessTermination::Cancelled, QStringLiteral("process cancellation"));
}

void testProbeTrimAndValidation(TestRunner &test)
{
    QTemporaryDir temporary;
    test.check(temporary.isValid(), QStringLiteral("trim temp dir"));
    const QString input = QDir(temporary.path()).filePath(QStringLiteral("source clip.mp4"));
    const QString output = QDir(temporary.path()).filePath(QStringLiteral("trimmed output.mp4"));
    test.check(writeFile(input, "input-media\n"), QStringLiteral("trim input"));

    FfmpegTools tools;
    QString toolError;
    test.check(
        createFakeToolPair(
            QDir(temporary.path()).filePath(QStringLiteral("tools")),
            &tools,
            &toolError),
        QStringLiteral("trim fake tool pair: %1").arg(toolError));
    if (!tools.isComplete()) {
        test.check(false, QStringLiteral("trim fake tool pair contract: %1 %2")
                              .arg(tools.trimCapabilityError(true), tools.probeCapabilityError()));
        return;
    }
    const ProbeResult probe = probeMedia(tools, input);
    test.check(probe.success, QStringLiteral("fake ffprobe parsed"));
    test.equal(probe.media.durationMs, qint64{10'000}, QStringLiteral("probe duration"));
    test.check(
        probe.media.hasVideo && probe.media.hasAudio && probe.media.videoSize == QSize(1920, 1080),
        QStringLiteral("probe stream metadata"));

    TrimRequest request;
    request.inputPath = input;
    request.outputPath = output;
    request.range = {1'250, 3'750};
    request.processTimeoutMs = 5'000;
    QVector<TrimProgress> progressEvents;
    const Qt::HANDLE invokingThread = QThread::currentThreadId();
    bool callbackStayedOnInvokingThread = true;
    const TrimResult result = runTrim(
        tools,
        request,
        {},
        [&](const TrimProgress &progress) {
            callbackStayedOnInvokingThread = callbackStayedOnInvokingThread
                && QThread::currentThreadId() == invokingThread;
            progressEvents.append(progress);
        });
    test.check(result.success, QStringLiteral("validated accurate trim: %1").arg(result.error));
    test.check(QFileInfo(output).size() > 0, QStringLiteral("trim committed"));
    test.equal(result.outputProbe.durationMs, qint64{2'500}, QStringLiteral("output probed duration"));
    test.check(result.validation.success, QStringLiteral("output validation"));
    test.check(callbackStayedOnInvokingThread, QStringLiteral("progress callback thread contract"));
    test.check(
        !progressEvents.isEmpty()
            && progressEvents.constFirst().stage == TrimStage::Preparing
            && progressEvents.constLast().stage == TrimStage::Completed,
        QStringLiteral("progress stage lifecycle"));
    const auto halfway = std::find_if(
        progressEvents.cbegin(),
        progressEvents.cend(),
        [](const TrimProgress &progress) {
            return progress.stage == TrimStage::Trimming && progress.percent == 50;
        });
    test.check(halfway != progressEvents.cend(), QStringLiteral("out_time_us normalized progress"));
    if (halfway != progressEvents.cend()) {
        test.equal(halfway->processedMs, qint64{1'250}, QStringLiteral("progress processed ms"));
        test.equal(halfway->totalMs, qint64{2'500}, QStringLiteral("progress total ms"));
        test.check(
            std::abs(halfway->fraction - 0.5) < 0.0001,
            QStringLiteral("progress normalized fraction"));
    }
    test.check(
        QDir(temporary.path()).entryList({QStringLiteral("*.trim-part.*")}, QDir::Files).isEmpty(),
        QStringLiteral("no trim partials left"));

    const QByteArray previous = [] (const QString &path) {
        QFile file(path);
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    }(output);
    const TrimResult noOverwrite = runTrim(tools, request);
    test.check(!noOverwrite.success, QStringLiteral("existing output rejected"));
    QFile outputFile(output);
    outputFile.open(QIODevice::ReadOnly);
    test.equal(outputFile.readAll(), previous, QStringLiteral("rejected trim preserves output"));
    outputFile.close();

    request.overwrite = true;
    test.check(writeFile(output, "old-output"), QStringLiteral("overwrite fixture"));
    const TrimResult replaced = runTrim(tools, request);
    test.check(replaced.success, QStringLiteral("transactional overwrite: %1").arg(replaced.error));
    QFile replacedFile(output);
    replacedFile.open(QIODevice::ReadOnly);
    test.check(replacedFile.readAll().startsWith("fake-media"), QStringLiteral("new output committed"));
    replacedFile.close();

    QFile::remove(output);
    request.overwrite = false;
    qputenv("BILLFISH_TEST_OUTPUT_DURATION_MS", "9000");
    const TrimResult invalidDuration = runTrim(tools, request);
    qunsetenv("BILLFISH_TEST_OUTPUT_DURATION_MS");
    test.check(!invalidDuration.success, QStringLiteral("duration mismatch rejected"));
    test.check(!QFileInfo::exists(output), QStringLiteral("invalid result not committed"));

    qputenv("BILLFISH_TEST_FAIL_FFMPEG", "1");
    const TrimResult failedProcess = runTrim(tools, request);
    qunsetenv("BILLFISH_TEST_FAIL_FFMPEG");
    test.check(!failedProcess.success, QStringLiteral("ffmpeg failure propagated"));
    test.check(
        failedProcess.error.contains(
            QDir::toNativeSeparators(tools.ffmpegPath), Qt::CaseInsensitive),
        QStringLiteral("ffmpeg failure identifies actual executable"));
    test.check(!QFileInfo::exists(output), QStringLiteral("failed process leaves no target"));
}

void testClipCache(TestRunner &test)
{
    QTemporaryDir temporary;
    const QString input = QDir(temporary.path()).filePath(QStringLiteral("input.mp4"));
    test.check(writeFile(input, "input"), QStringLiteral("cache input"));
    const QString cacheRoot = QDir(temporary.path()).filePath(QStringLiteral("cache"));
    ClipCache cache(cacheRoot);

    TrimRequest request;
    request.inputPath = input;
    request.outputPath = QDir(temporary.path()).filePath(QStringLiteral("requested.mp4"));
    request.range = {100, 900};
    TrimRequest unresolvedOutput = request;
    unresolvedOutput.outputPath.clear();
    const QString keyBeforeCachePath = cache.keyFor(unresolvedOutput);
    const QString provisionalCachePath = cache.outputPathFor(unresolvedOutput);
    unresolvedOutput.outputPath = provisionalCachePath;
    test.equal(
        cache.keyFor(unresolvedOutput),
        keyBeforeCachePath,
        QStringLiteral("cache key stable when assigning outputPathFor result"));
    test.equal(
        cache.outputPathFor(unresolvedOutput),
        provisionalCachePath,
        QStringLiteral("cache path stable after assignment"));
    test.equal(
        QFileInfo(provisionalCachePath).fileName(),
        QStringLiteral("input_0.1s-0.9s.mp4"),
        QStringLiteral("clipboard cache filename uses source name and seconds"));
    test.equal(
        QFileInfo(provisionalCachePath).dir().dirName(),
        keyBeforeCachePath,
        QStringLiteral("readable clipboard filenames remain collision-safe"));
    const QString managedOutput = cache.outputPathFor(request);
    test.check(
        QDir().mkpath(QFileInfo(managedOutput).absolutePath()),
        QStringLiteral("managed cache output directory"));
    test.check(writeFile(managedOutput, "cached-output"), QStringLiteral("managed cache output"));
    QString error;
    test.check(cache.record(request, managedOutput, &error), QStringLiteral("cache record: %1").arg(error));

    const QString key = cache.keyFor(request);
    const ClipCacheLookup first = cache.lookup(request);
    test.check(first.hit && first.entry.key == key, QStringLiteral("cache lookup"));
    test.check(QFileInfo::exists(cache.manifestPath()), QStringLiteral("persistent manifest"));
    const ClipCacheLookup persistent = ClipCache(cacheRoot).lookup(request, false);
    test.check(persistent.hit, QStringLiteral("cache survives new instance"));

    const ClipCachePruneResult protectedPrune = cache.prune(0, 0, QSet<QString>{key});
    test.check(
        protectedPrune.success && !protectedPrune.limitsSatisfied
            && protectedPrune.removedEntries == 0 && QFileInfo::exists(managedOutput),
        QStringLiteral("prune preserves protected clipboard clip"));
    const ClipCachePruneResult pruned = cache.prune(0, 0);
    test.check(
        pruned.success && pruned.limitsSatisfied && pruned.removedEntries == 1,
        QStringLiteral("LRU prune"));
    test.check(!QFileInfo::exists(managedOutput), QStringLiteral("prune removes managed clip"));
    test.check(!cache.lookup(request).hit, QStringLiteral("pruned cache miss"));

    const QString migrationRoot = QDir(temporary.path()).filePath(
        QStringLiteral("migration-cache"));
    ClipCache migrationCache(migrationRoot);
    TrimRequest copyRequest = request;
    copyRequest.outputPath = migrationCache.outputPathFor(copyRequest);
    const QString legacyManagedOutput = QDir(migrationRoot).filePath(
        QStringLiteral("clips/%1.mp4").arg(migrationCache.keyFor(copyRequest)));
    test.check(
        QDir().mkpath(QFileInfo(legacyManagedOutput).absolutePath()) &&
            writeFile(legacyManagedOutput, "legacy-cache-output"),
        QStringLiteral("legacy hash-only cache fixture"));
    test.check(
        migrationCache.record(copyRequest, legacyManagedOutput, &error),
        QStringLiteral("legacy hash-only cache record"));
    const ClipCacheLookup migratedLookup = migrationCache.lookup(copyRequest);
    test.check(
        !migratedLookup.hit && migratedLookup.error.isEmpty(),
        QStringLiteral("legacy hash-only cache is invalidated"));
    test.check(
        !QFileInfo::exists(legacyManagedOutput),
        QStringLiteral("legacy hash-only cache file is removed"));

    const QString externalOutput = QDir(temporary.path()).filePath(QStringLiteral("external.mp4"));
    test.check(writeFile(externalOutput, "external-output"), QStringLiteral("external cache fixture"));
    test.check(cache.record(request, externalOutput, &error), QStringLiteral("external cache record"));
    test.check(cache.clear(&error), QStringLiteral("cache clear"));
    test.check(QFileInfo::exists(externalOutput), QStringLiteral("clear never deletes unmanaged output"));

    test.check(writeFile(cache.manifestPath(), "not-json"), QStringLiteral("corrupt manifest fixture"));
    error.clear();
    const QVector<ClipCacheEntry> corrupt = cache.entries(&error);
    test.check(corrupt.isEmpty() && !error.isEmpty(), QStringLiteral("corrupt manifest reported"));
}

QString findTestSqliteLibrary()
{
    QStringList candidates{
        QString::fromLocal8Bit(qgetenv("BILLFISH_TEST_SQLITE3_DLL")),
        QString::fromLocal8Bit(qgetenv("BILLFISH_SQLITE3")),
    };
    QList<QDir> roots{
        QDir(QCoreApplication::applicationDirPath()),
        QDir::current(),
        QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()),
    };
    for (QDir root : roots) {
        for (int depth = 0; depth < 8; ++depth) {
            candidates.append(root.filePath(QStringLiteral("sqlite3.dll")));
            if (!root.cdUp()) {
                break;
            }
        }
    }
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

bool createTestDatabase(
    const QString &sqliteLibrary,
    const QString &databasePath,
    QString *error,
    bool includeVideoMetadata = true)
{
    struct sqlite3;
    using OpenFunction = int (*)(const char *, sqlite3 **, int, const char *);
    using ExecFunction = int (*)(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
    using CloseFunction = int (*)(sqlite3 *);
    using FreeFunction = void (*)(void *);

    if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        *error = QStringLiteral("sqlite test database directory could not be created");
        return false;
    }

    QLibrary library(sqliteLibrary);
    if (!library.load()) {
        *error = library.errorString();
        return false;
    }
    const auto open = reinterpret_cast<OpenFunction>(library.resolve("sqlite3_open_v2"));
    const auto exec = reinterpret_cast<ExecFunction>(library.resolve("sqlite3_exec"));
    const auto close = reinterpret_cast<CloseFunction>(library.resolve("sqlite3_close"));
    const auto freeMemory = reinterpret_cast<FreeFunction>(library.resolve("sqlite3_free"));
    if (!open || !exec || !close || !freeMemory) {
        *error = QStringLiteral("sqlite test runtime is missing required symbols");
        return false;
    }

    sqlite3 *database = nullptr;
    constexpr int readWriteCreate = 0x00000002 | 0x00000004;
    const QByteArray databaseUtf8 = QFileInfo(databasePath).absoluteFilePath().toUtf8();
    if (open(databaseUtf8.constData(), &database, readWriteCreate, nullptr) != 0 || !database) {
        *error = QStringLiteral("sqlite test database could not be opened");
        if (database) {
            close(database);
        }
        return false;
    }
    QByteArray sql =
        "CREATE TABLE bf_file (id INTEGER PRIMARY KEY, name TEXT, pid INTEGER);"
        "CREATE TABLE bf_folder (id INTEGER PRIMARY KEY, name TEXT, pid INTEGER);"
        "INSERT INTO bf_folder VALUES (10, 'Folder', 0);"
        "INSERT INTO bf_folder VALUES (20, 'Nested', 10);"
        "INSERT INTO bf_file VALUES (42, 'clip.mp4', 20);"
        "INSERT INTO bf_file VALUES (43, '../escape.mp4', 0);"
        "INSERT INTO bf_folder VALUES (30, 'CycleA', 31);"
        "INSERT INTO bf_folder VALUES (31, 'CycleB', 30);"
        "INSERT INTO bf_file VALUES (44, 'cycle.mp4', 30);"
        "INSERT INTO bf_file VALUES (45, 'offline.mp4', 20);";
    if (includeVideoMetadata) {
        sql +=
            "CREATE TABLE bf_material_video (id INTEGER PRIMARY KEY, file_id INTEGER UNIQUE, duration INTEGER);"
            "INSERT INTO bf_material_video VALUES (1, 42, 4321);";
    }
    char *sqliteError = nullptr;
    const int status = exec(database, sql.constData(), nullptr, nullptr, &sqliteError);
    if (status != 0) {
        *error = sqliteError ? QString::fromUtf8(sqliteError) : QStringLiteral("sqlite exec failed");
        if (sqliteError) {
            freeMemory(sqliteError);
        }
        close(database);
        return false;
    }
    return close(database) == 0;
}

void testBillfishLocator(TestRunner &test)
{
    QTemporaryDir temporary;
    const QString library = QDir(temporary.path()).filePath(QStringLiteral("Library"));
    const QString dbPath = QDir(library).filePath(QStringLiteral(".bf/billfish.db"));
    const QString mediaPath = QDir(library).filePath(QStringLiteral("Folder/Nested/clip.mp4"));
    test.check(writeFile(mediaPath, "media"), QStringLiteral("Billfish media fixture"));
    test.check(QDir().mkpath(QFileInfo(dbPath).absolutePath()), QStringLiteral("Billfish .bf fixture"));

    const QString sqliteDll = findTestSqliteLibrary();
    QString error;
    if (sqliteDll.isEmpty()) {
        QTextStream(stdout) << "SKIP: sqlite3.dll unavailable; Billfish locator runtime test" << Qt::endl;
        return;
    }
    test.check(createTestDatabase(sqliteDll, dbPath, &error), QStringLiteral("create test db: %1").arg(error));

    BillfishFileLocator locator(library, sqliteDll);
    const BillfishFileLookup found = locator.locate(42);
    test.check(found.success, QStringLiteral("Billfish lookup: %1").arg(found.error));
    test.equal(found.file.relativePath, QStringLiteral("Folder/Nested/clip.mp4"), QStringLiteral("folder reconstruction"));
    test.equal(QDir::cleanPath(found.file.absolutePath), QDir::cleanPath(mediaPath), QStringLiteral("absolute media path"));
    test.check(found.file.exists, QStringLiteral("located file exists"));
    test.equal(found.file.durationMs, qint64{4'321}, QStringLiteral("Billfish video duration"));

    test.check(!locator.locate(999).success, QStringLiteral("missing Billfish id rejected"));
    test.check(!locator.locate(43).success, QStringLiteral("path traversal rejected"));
    test.check(!locator.locate(44).success, QStringLiteral("folder cycle rejected"));
    const BillfishFileLookup offline = locator.locate(45);
    test.check(
        offline.success && !offline.file.exists && offline.file.durationMs == -1,
        QStringLiteral("offline file path still resolved"));

    const QString legacyLibrary = QDir(temporary.path()).filePath(QStringLiteral("LegacyLibrary"));
    const QString legacyDb = QDir(legacyLibrary).filePath(QStringLiteral(".bf/billfish.db"));
    const QString legacyMedia = QDir(legacyLibrary).filePath(QStringLiteral("Folder/Nested/clip.mp4"));
    test.check(writeFile(legacyMedia, "legacy"), QStringLiteral("legacy Billfish media fixture"));
    test.check(
        createTestDatabase(sqliteDll, legacyDb, &error, false),
        QStringLiteral("create legacy test db: %1").arg(error));
    const BillfishFileLookup legacy = BillfishFileLocator(legacyLibrary, sqliteDll).locate(42);
    test.check(
        legacy.success && legacy.file.durationMs == -1,
        QStringLiteral("legacy schema without video metadata remains supported"));

    const QString configPath = QDir(temporary.path()).filePath(QStringLiteral("recent_lib.ini"));
    {
        QSettings settings(configPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("LoadDefaultLibType/LastLoadLibPath"), library);
        settings.setValue(QStringLiteral("ResourceLibsVector/2/Path"), library);
        settings.sync();
    }
    const QStringList roots = BillfishFileLocator::discoverLibraryRoots(configPath);
    test.equal(roots.size(), 1, QStringLiteral("library discovery de-duplicates roots"));
    test.equal(QDir::cleanPath(roots.value(0)), QDir::cleanPath(library), QStringLiteral("library discovery path"));
    test.equal(
        BillfishFileLocator::findSqliteLibrary({}, sqliteDll),
        QFileInfo(sqliteDll).absoluteFilePath(),
        QStringLiteral("explicit sqlite runtime discovery"));
}

void testClipboardPayload(TestRunner &test)
{
    QTemporaryDir temporary;
    const QString first = QDir(temporary.path()).filePath(QStringLiteral("first clip.mp4"));
    const QString second = QDir(temporary.path()).filePath(QStringLiteral("second clip.mp4"));
    test.check(writeFile(first, "a") && writeFile(second, "b"), QStringLiteral("clipboard fixtures"));
    QString error;
    const QByteArray payload = WindowsFileClipboard::buildHDropPayload({first, second}, &error);
    test.check(!payload.isEmpty() && error.isEmpty(), QStringLiteral("CF_HDROP payload: %1").arg(error));
#ifdef Q_OS_WIN
    test.check(payload.size() > static_cast<int>(sizeof(DROPFILES)), QStringLiteral("CF_HDROP payload size"));
    DROPFILES header{};
    std::memcpy(&header, payload.constData(), sizeof(header));
    test.equal(header.pFiles, DWORD{sizeof(DROPFILES)}, QStringLiteral("DROPFILES offset"));
    test.check(header.fWide == TRUE, QStringLiteral("DROPFILES Unicode flag"));
    const ushort *names = reinterpret_cast<const ushort *>(payload.constData() + header.pFiles);
    const QString decodedFirst = QString::fromUtf16(names);
    test.equal(
        QDir::fromNativeSeparators(decodedFirst),
        QDir::fromNativeSeparators(QFileInfo(first).absoluteFilePath()),
        QStringLiteral("CF_HDROP first path"));
#endif
    const QByteArray effect = WindowsFileClipboard::buildPreferredDropEffectPayload(DropEffect::Copy);
    quint32 effectValue = 0;
    std::memcpy(&effectValue, effect.constData(), sizeof(effectValue));
    test.equal(effectValue, quint32{1}, QStringLiteral("Preferred DropEffect COPY"));
    const QByteArray invalidPayload = WindowsFileClipboard::buildHDropPayload(
        {QStringLiteral("relative.mp4")},
        &error);
    Q_UNUSED(invalidPayload);
    test.check(!error.isEmpty(), QStringLiteral("relative clipboard path rejected"));
}

void testRealFfmpegSmokeWhenRequested(TestRunner &test)
{
    const QString toolDirectory = QString::fromLocal8Bit(
        qgetenv("BILLFISH_TEST_REAL_FFMPEG_DIR"));
    if (toolDirectory.isEmpty()) {
        return;
    }
    const FfmpegTools tools = locateFfmpegTools(toolDirectory);
    test.check(tools.isComplete(), QStringLiteral("real FFmpeg tools discovered"));
    if (!tools.isComplete()) {
        return;
    }

    QTemporaryDir temporary;
    const QString input = QDir(temporary.path()).filePath(QStringLiteral("real input.mp4"));
    ProcessSpec generate;
    generate.program = tools.ffmpegPath;
    generate.timeoutMs = 30'000;
    generate.arguments = QStringList{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-f"),
        QStringLiteral("lavfi"),
        QStringLiteral("-i"),
        QStringLiteral("testsrc2=size=160x90:rate=30:duration=4"),
        QStringLiteral("-f"),
        QStringLiteral("lavfi"),
        QStringLiteral("-i"),
        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"),
        QStringLiteral("-c:v"),
        QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"),
        QStringLiteral("aac"),
        QStringLiteral("-shortest"),
        input,
    };
    const ProcessResult generated = runProcess(generate);
    test.check(generated.succeeded(), QStringLiteral("real media generated: %1").arg(generated.error));
    if (!generated.succeeded()) {
        return;
    }

    TrimRequest accurate;
    accurate.inputPath = input;
    accurate.outputPath = QDir(temporary.path()).filePath(QStringLiteral("accurate output.mp4"));
    accurate.range = {750, 2'750};
    accurate.processTimeoutMs = 30'000;
    accurate.validationToleranceMs = 250;
    QVector<TrimProgress> accurateProgress;
    const TrimResult accurateResult = runTrim(
        tools,
        accurate,
        {},
        [&](const TrimProgress &progress) { accurateProgress.append(progress); });
    test.check(
        accurateResult.success,
        QStringLiteral("real accurate trim: %1").arg(accurateResult.error));
    test.check(
        accurateResult.process.standardOutput.contains("out_time_us=")
            && !accurateProgress.isEmpty()
            && accurateProgress.constLast().stage == TrimStage::Completed,
        QStringLiteral("real accurate progress stream"));

    TrimRequest fast = accurate;
    fast.outputPath = QDir(temporary.path()).filePath(QStringLiteral("fast output.mp4"));
    fast.mode = TrimMode::FastCopy;
    fast.validationToleranceMs = 2'500;
    QVector<TrimProgress> fastProgress;
    const TrimResult fastResult = runTrim(
        tools,
        fast,
        {},
        [&](const TrimProgress &progress) { fastProgress.append(progress); });
    test.check(fastResult.success, QStringLiteral("real fast trim: %1").arg(fastResult.error));
    test.check(
        fastResult.process.standardOutput.contains("out_time_us=")
            && !fastProgress.isEmpty()
            && fastProgress.constLast().stage == TrimStage::Completed,
        QStringLiteral("real fast progress stream"));
}

} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QCoreApplication application(argc, argv);
    const int childResult = runChildMode(application.arguments());
    if (childResult >= 0) {
        return childResult;
    }

    TestRunner test;
    testRangeSelection(test);
    testToolDiscoveryAndArguments(test);
    testProcessExecution(test);
    testProbeTrimAndValidation(test);
    testClipCache(test);
    testBillfishLocator(test);
    testClipboardPayload(test);
    testRealFfmpegSmokeWhenRequested(test);
    return test.finish();
}
