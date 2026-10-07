#include "ffmpeg_tools.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace billfish::trim {
namespace {

QString existingFile(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    return info.exists() && info.isFile() ? info.absoluteFilePath() : QString();
}

QString findNamedFile(const QString &pathOrDirectory, const QStringList &names)
{
    if (pathOrDirectory.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo info(pathOrDirectory);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }

    const QDir directory(info.isDir() ? info.absoluteFilePath() : pathOrDirectory);
    for (const QString &name : names) {
        const QString candidate = existingFile(directory.filePath(name));
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    return {};
}

QString executableDisplayPath(const QString &path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    return QDir::toNativeSeparators(absolutePath.isEmpty() ? path : absolutePath);
}

#ifdef Q_OS_WIN
bool isPortableExecutable(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray dosHeader = file.read(64);
    if (dosHeader.size() != 64 || dosHeader.at(0) != 'M' || dosHeader.at(1) != 'Z') {
        return false;
    }
    const auto byte = [&](int index) {
        return static_cast<quint32>(static_cast<unsigned char>(dosHeader.at(index)));
    };
    const quint32 peOffset = byte(0x3c)
        | (byte(0x3d) << 8U)
        | (byte(0x3e) << 16U)
        | (byte(0x3f) << 24U);
    if (peOffset < 64 || peOffset > static_cast<quint32>(file.size() - 4)) {
        return false;
    }
    if (!file.seek(peOffset)) {
        return false;
    }
    return file.read(4) == QByteArray("PE\0\0", 4);
}
#endif

struct ToolInspection {
    bool cli = false;
    bool libx264Encoder = false;
    bool libx264Crf = false;
    QString version;
    QString error;
};

QString inspectionCacheKey(const QString &path, const QString &role)
{
    const QFileInfo info(path);
    QString normalized = info.canonicalFilePath();
    if (normalized.isEmpty()) {
        normalized = info.absoluteFilePath();
    }
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return QStringLiteral("%1|%2|%3|%4")
        .arg(role, normalized)
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

QString contractOutput(const ProcessResult &result)
{
    QByteArray output = result.standardOutput;
    if (!result.standardError.isEmpty()) {
        if (!output.isEmpty() && !output.endsWith('\n')) {
            output.append('\n');
        }
        output.append(result.standardError);
    }
    return QString::fromUtf8(output);
}

QString findVersionLine(const QString &output, const QString &prefix)
{
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(prefix, Qt::CaseInsensitive)) {
            return trimmed;
        }
    }
    return {};
}

ToolInspection inspectToolUncached(const QString &path, bool ffmpeg)
{
    ToolInspection inspection;
    const QString executable = existingFile(path);
    const QString role = ffmpeg ? QStringLiteral("FFmpeg") : QStringLiteral("FFprobe");
    if (executable.isEmpty()) {
        inspection.error = path.trimmed().isEmpty()
            ? QStringLiteral("%1 executable was not found.").arg(role)
            : QStringLiteral("%1 executable does not exist: \"%2\"")
                  .arg(role, executableDisplayPath(path));
        return inspection;
    }
#ifdef Q_OS_WIN
    if (ffmpeg
        && QFileInfo(executable).fileName().compare(
               QStringLiteral("ffmpegLite64.exe"), Qt::CaseInsensitive) == 0) {
        inspection.error = QStringLiteral(
                               "FFmpeg executable \"%1\" is Billfish's unsupported ffmpegLite64 wrapper, "
                               "not a full FFmpeg CLI.")
                               .arg(executableDisplayPath(executable));
        return inspection;
    }
    if (!isPortableExecutable(executable)) {
        inspection.error = QStringLiteral(
                               "%1 executable \"%2\" is not a valid Windows PE executable.")
                               .arg(role, executableDisplayPath(executable));
        return inspection;
    }
#endif

    ProcessSpec versionSpec;
    versionSpec.program = executable;
    versionSpec.arguments = QStringList{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-version"),
    };
    versionSpec.startTimeoutMs = 3'000;
    versionSpec.timeoutMs = 3'000;
    const ProcessResult versionResult = runProcess(versionSpec);
    const QString versionOutput = contractOutput(versionResult);
    const QString expectedPrefix = ffmpeg
        ? QStringLiteral("ffmpeg version ")
        : QStringLiteral("ffprobe version ");
    const QString versionLine = findVersionLine(versionOutput, expectedPrefix);
    if (!versionResult.succeeded() || versionLine.isEmpty()) {
        QString detail = versionResult.error.trimmed();
        if (detail.isEmpty()) {
            detail = versionOutput.trimmed().left(500);
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("the -version output did not identify the expected CLI");
        }
        inspection.error = QStringLiteral(
                               "%1 executable \"%2\" failed the standard CLI identity contract: %3")
                               .arg(role, executableDisplayPath(executable), detail);
        return inspection;
    }

    inspection.cli = true;
    inspection.version = versionLine;
    if (!ffmpeg) {
        return inspection;
    }

    ProcessSpec encoderSpec;
    encoderSpec.program = executable;
    encoderSpec.arguments = QStringList{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-h"),
        QStringLiteral("encoder=libx264"),
    };
    encoderSpec.startTimeoutMs = 3'000;
    encoderSpec.timeoutMs = 3'000;
    const ProcessResult encoderResult = runProcess(encoderSpec);
    const QString encoderOutput = contractOutput(encoderResult);
    inspection.libx264Encoder = encoderResult.succeeded()
        && encoderOutput.contains(QStringLiteral("Encoder libx264"), Qt::CaseInsensitive);
    static const QRegularExpression crfOption(
        QStringLiteral("(^|[\\r\\n])\\s*-crf(?:\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    inspection.libx264Crf = inspection.libx264Encoder
        && crfOption.match(encoderOutput).hasMatch();
    return inspection;
}

ToolInspection inspectTool(const QString &path, bool ffmpeg)
{
    static QMutex cacheMutex;
    static QHash<QString, ToolInspection> cache;
    const QString key = inspectionCacheKey(
        path,
        ffmpeg ? QStringLiteral("ffmpeg") : QStringLiteral("ffprobe"));
    {
        QMutexLocker locker(&cacheMutex);
        const auto found = cache.constFind(key);
        if (found != cache.cend()) {
            return found.value();
        }
    }

    const ToolInspection inspected = inspectToolUncached(path, ffmpeg);
    QMutexLocker locker(&cacheMutex);
    cache.insert(key, inspected);
    return inspected;
}

struct ToolCandidate {
    QString ffmpegPath;
    QString ffprobePath;
    QString source;
};

void appendCandidate(
    QVector<ToolCandidate> *candidates,
    QSet<QString> *visited,
    const QString &ffmpegPath,
    const QString &ffprobePath,
    const QString &source)
{
    if (ffmpegPath.isEmpty() && ffprobePath.isEmpty()) {
        return;
    }
    QString key = QFileInfo(ffmpegPath).absoluteFilePath()
        + QLatin1Char('|') + QFileInfo(ffprobePath).absoluteFilePath();
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    if (visited->contains(key)) {
        return;
    }
    visited->insert(key);
    candidates->append({ffmpegPath, ffprobePath, source});
}

void appendDirectoryCandidate(
    QVector<ToolCandidate> *candidates,
    QSet<QString> *visited,
    const QString &directory,
    const QString &source,
    const QStringList &ffmpegNames,
    const QStringList &ffprobeNames)
{
    if (directory.trimmed().isEmpty()) {
        return;
    }
    appendCandidate(
        candidates,
        visited,
        findNamedFile(directory, ffmpegNames),
        findNamedFile(directory, ffprobeNames),
        source);
}

double jsonDouble(const QJsonValue &value, bool *ok)
{
    if (value.isDouble()) {
        *ok = true;
        return value.toDouble();
    }
    if (value.isString()) {
        return value.toString().toDouble(ok);
    }
    *ok = false;
    return 0.0;
}

qint64 secondsToMilliseconds(const QJsonValue &value)
{
    bool ok = false;
    const double seconds = jsonDouble(value, &ok);
    if (!ok || !std::isfinite(seconds) || seconds < 0.0
        || seconds > static_cast<double>(std::numeric_limits<qint64>::max()) / 1'000.0) {
        return -1;
    }
    return static_cast<qint64>(std::llround(seconds * 1'000.0));
}

double parseFrameRate(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('/'));
    bool numeratorOk = false;
    const double numerator = parts.value(0).toDouble(&numeratorOk);
    if (!numeratorOk || !std::isfinite(numerator) || numerator <= 0.0) {
        return 0.0;
    }
    if (parts.size() == 1) {
        return numerator;
    }
    bool denominatorOk = false;
    const double denominator = parts.value(1).toDouble(&denominatorOk);
    if (!denominatorOk || !std::isfinite(denominator) || denominator <= 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

int boundedWait(qint64 milliseconds)
{
    return static_cast<int>(std::clamp<qint64>(milliseconds, 0, std::numeric_limits<int>::max()));
}

void stopProcess(QProcess &process)
{
    if (process.state() == QProcess::NotRunning) {
        return;
    }
    process.terminate();
    if (!process.waitForFinished(1'000)) {
        process.kill();
        process.waitForFinished(2'000);
    }
}

} // namespace

bool FfmpegCapabilities::canFastTrim() const noexcept
{
    return ffmpegCli;
}

bool FfmpegCapabilities::canAccurateTrim() const noexcept
{
    return ffmpegCli && libx264Encoder && libx264Crf;
}

bool FfmpegCapabilities::canProbe() const noexcept
{
    return ffprobeCli;
}

bool FfmpegCapabilities::isComplete() const noexcept
{
    return canFastTrim() && canProbe();
}

FfmpegCapabilities FfmpegTools::capabilities() const
{
    const ToolInspection ffmpeg = inspectTool(ffmpegPath, true);
    const ToolInspection ffprobe = inspectTool(ffprobePath, false);
    FfmpegCapabilities result;
    result.ffmpegCli = ffmpeg.cli;
    result.ffprobeCli = ffprobe.cli;
    result.libx264Encoder = ffmpeg.libx264Encoder;
    result.libx264Crf = ffmpeg.libx264Crf;
    result.ffmpegVersion = ffmpeg.version;
    result.ffprobeVersion = ffprobe.version;
    result.ffmpegError = ffmpeg.error;
    result.ffprobeError = ffprobe.error;
    return result;
}

bool FfmpegTools::canTrim() const
{
    return inspectTool(ffmpegPath, true).cli;
}

bool FfmpegTools::canAccurateTrim() const
{
    const ToolInspection ffmpeg = inspectTool(ffmpegPath, true);
    return ffmpeg.cli && ffmpeg.libx264Encoder && ffmpeg.libx264Crf;
}

bool FfmpegTools::canProbe() const
{
    return inspectTool(ffprobePath, false).cli;
}

bool FfmpegTools::isComplete() const
{
    return canTrim() && canProbe();
}

QString FfmpegTools::trimCapabilityError(bool requireAccurate) const
{
    const ToolInspection ffmpeg = inspectTool(ffmpegPath, true);
    if (!ffmpeg.cli) {
        return !ffmpeg.error.isEmpty() ? ffmpeg.error : discoveryError;
    }
    if (requireAccurate && (!ffmpeg.libx264Encoder || !ffmpeg.libx264Crf)) {
        QStringList missing;
        if (!ffmpeg.libx264Encoder) {
            missing.append(QStringLiteral("libx264 encoder"));
        }
        if (!ffmpeg.libx264Crf) {
            missing.append(QStringLiteral("libx264 -crf option"));
        }
        return QStringLiteral(
                   "Accurate trimming requires %1, but FFmpeg executable \"%2\" does not expose it.")
                   .arg(missing.join(QStringLiteral(" and ")), executableDisplayPath(ffmpegPath));
    }
    return {};
}

QString FfmpegTools::probeCapabilityError() const
{
    const ToolInspection ffprobe = inspectTool(ffprobePath, false);
    if (ffprobe.cli) {
        return {};
    }
    return !ffprobe.error.isEmpty() ? ffprobe.error : discoveryError;
}

bool ProcessResult::succeeded() const noexcept
{
    return termination == ProcessTermination::Completed && exitCode == 0;
}

bool MediaProbe::isValid() const noexcept
{
    return durationMs >= 0 && (hasVideo || hasAudio);
}

FfmpegTools locateFfmpegTools(
    const QString &applicationDirectory,
    const QString &explicitFfmpeg,
    const QString &explicitFfprobe,
    const QStringList &additionalSearchDirectories)
{
#ifdef Q_OS_WIN
    const QStringList ffmpegNames{QStringLiteral("ffmpeg.exe")};
    const QStringList ffprobeNames{QStringLiteral("ffprobe.exe")};
#else
    const QStringList ffmpegNames{QStringLiteral("ffmpeg")};
    const QStringList ffprobeNames{QStringLiteral("ffprobe")};
#endif

    QVector<ToolCandidate> candidates;
    QSet<QString> visited;

    QString explicitFfmpegPath = findNamedFile(explicitFfmpeg, ffmpegNames);
    QString explicitFfprobePath = findNamedFile(explicitFfprobe, ffprobeNames);
    if (!explicitFfmpegPath.isEmpty() && explicitFfprobePath.isEmpty()) {
        explicitFfprobePath = findNamedFile(
            QFileInfo(explicitFfmpegPath).absolutePath(), ffprobeNames);
    }
    if (explicitFfmpegPath.isEmpty() && !explicitFfprobePath.isEmpty()) {
        explicitFfmpegPath = findNamedFile(
            QFileInfo(explicitFfprobePath).absolutePath(), ffmpegNames);
    }
    appendCandidate(
        &candidates,
        &visited,
        explicitFfmpegPath,
        explicitFfprobePath,
        QStringLiteral("explicit arguments"));

    const QString environmentFfmpeg = qEnvironmentVariable("BILLFISH_FFMPEG");
    const QString environmentFfprobe = qEnvironmentVariable("BILLFISH_FFPROBE");
    QString environmentFfmpegPath = findNamedFile(environmentFfmpeg, ffmpegNames);
    QString environmentFfprobePath = findNamedFile(environmentFfprobe, ffprobeNames);
    if (!environmentFfmpegPath.isEmpty() && environmentFfprobePath.isEmpty()) {
        environmentFfprobePath = findNamedFile(
            QFileInfo(environmentFfmpegPath).absolutePath(), ffprobeNames);
    }
    if (environmentFfmpegPath.isEmpty() && !environmentFfprobePath.isEmpty()) {
        environmentFfmpegPath = findNamedFile(
            QFileInfo(environmentFfprobePath).absolutePath(), ffmpegNames);
    }
    appendCandidate(
        &candidates,
        &visited,
        environmentFfmpegPath,
        environmentFfprobePath,
        QStringLiteral("BILLFISH_FFMPEG/BILLFISH_FFPROBE"));

    const QString ffmpegHome = qEnvironmentVariable("FFMPEG_HOME");
    if (!ffmpegHome.trimmed().isEmpty()) {
        appendDirectoryCandidate(
            &candidates,
            &visited,
            QDir(ffmpegHome).filePath(QStringLiteral("bin")),
            QStringLiteral("FFMPEG_HOME/bin"),
            ffmpegNames,
            ffprobeNames);
        appendDirectoryCandidate(
            &candidates,
            &visited,
            ffmpegHome,
            QStringLiteral("FFMPEG_HOME"),
            ffmpegNames,
            ffprobeNames);
    }

    const QString pathFfmpeg = QStandardPaths::findExecutable(ffmpegNames.constFirst());
    QString pathFfprobe;
    if (!pathFfmpeg.isEmpty()) {
        pathFfprobe = findNamedFile(QFileInfo(pathFfmpeg).absolutePath(), ffprobeNames);
    } else {
        pathFfprobe = QStandardPaths::findExecutable(ffprobeNames.constFirst());
    }
    appendCandidate(
        &candidates,
        &visited,
        pathFfmpeg,
        pathFfprobe,
        QStringLiteral("PATH (same-directory pair)"));

    for (const QString &directory : additionalSearchDirectories) {
        appendDirectoryCandidate(
            &candidates,
            &visited,
            directory,
            QStringLiteral("additional supported directory"),
            ffmpegNames,
            ffprobeNames);
    }

    QString appDirectory = applicationDirectory;
    if (appDirectory.isEmpty() && QCoreApplication::instance()) {
        appDirectory = QCoreApplication::applicationDirPath();
    }
    if (!appDirectory.trimmed().isEmpty()) {
        const QStringList applicationToolDirectories{
            QDir(appDirectory).filePath(QStringLiteral("ffmpeg/bin")),
            QDir(appDirectory).filePath(QStringLiteral("tools/ffmpeg/bin")),
            QDir(appDirectory).filePath(QStringLiteral("bin")),
            QDir(appDirectory).filePath(QStringLiteral("ffmpeg")),
            appDirectory,
        };
        for (const QString &directory : applicationToolDirectories) {
            appendDirectoryCandidate(
                &candidates,
                &visited,
                directory,
                QStringLiteral("application supported directory"),
                ffmpegNames,
                ffprobeNames);
        }
    }

    FfmpegTools ffprobeFallback;
    FfmpegTools ffmpegFallback;
    QStringList rejected;
    for (const ToolCandidate &candidate : candidates) {
        FfmpegTools tools;
        tools.ffmpegPath = candidate.ffmpegPath;
        tools.ffprobePath = candidate.ffprobePath;
        const bool canTrim = tools.canTrim();
        const bool canProbe = tools.canProbe();
        if (canTrim && canProbe) {
            return tools;
        }
        if (canProbe && ffprobeFallback.ffprobePath.isEmpty()) {
            ffprobeFallback.ffprobePath = tools.ffprobePath;
        }
        if (canTrim && ffmpegFallback.ffmpegPath.isEmpty()) {
            ffmpegFallback.ffmpegPath = tools.ffmpegPath;
        }

        QStringList reasons;
        if (!tools.ffmpegPath.isEmpty() && !canTrim) {
            reasons.append(tools.trimCapabilityError(false));
        }
        if (!tools.ffprobePath.isEmpty() && !canProbe) {
            reasons.append(tools.probeCapabilityError());
        }
        if (!reasons.isEmpty() && rejected.size() < 5) {
            rejected.append(QStringLiteral("%1: %2")
                                .arg(candidate.source, reasons.join(QStringLiteral(" "))));
        }
    }

    FfmpegTools fallback = !ffprobeFallback.ffprobePath.isEmpty()
        ? ffprobeFallback
        : ffmpegFallback;
    fallback.discoveryError = rejected.isEmpty()
        ? QStringLiteral("No supported FFmpeg/FFprobe executable pair was found.")
        : rejected.join(QLatin1Char('\n'));
    return fallback;
}

QStringList buildProbeArguments(const QString &inputPath)
{
    return {
        QStringLiteral("-v"),
        QStringLiteral("error"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format=duration,format_name,start_time:stream=index,codec_type,codec_name,width,height,avg_frame_rate,r_frame_rate,duration"),
        QStringLiteral("-of"),
        QStringLiteral("json"),
        QStringLiteral("--"),
        inputPath,
    };
}

ProcessResult runProcess(
    const ProcessSpec &spec,
    const std::function<bool()> &cancelRequested,
    const ProcessOutputCallback &outputReceived)
{
    ProcessResult result;
    QElapsedTimer timer;
    timer.start();

    if (spec.program.trimmed().isEmpty()) {
        result.termination = ProcessTermination::StartFailed;
        result.error = QStringLiteral("The process program path is empty.");
        return result;
    }
    const QString executable = executableDisplayPath(spec.program);

    QProcess process;
    process.setProgram(spec.program);
    process.setArguments(spec.arguments);
    if (!spec.workingDirectory.isEmpty()) {
        process.setWorkingDirectory(spec.workingDirectory);
    }

    QProcessEnvironment environment = spec.inheritSystemEnvironment
        ? QProcessEnvironment::systemEnvironment()
        : QProcessEnvironment();
    for (const QString &key : spec.environment.keys()) {
        environment.insert(key, spec.environment.value(key));
    }
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(QIODevice::ReadOnly);

    if (!process.waitForStarted(std::max(0, spec.startTimeoutMs))) {
        result.termination = ProcessTermination::StartFailed;
        result.error = QStringLiteral("Could not start executable \"%1\": %2")
                           .arg(executable, process.errorString());
        result.elapsedMs = timer.elapsed();
        return result;
    }

    const auto drainOutput = [&] {
        const QByteArray standardOutputChunk = process.readAllStandardOutput();
        const QByteArray standardErrorChunk = process.readAllStandardError();
        result.standardOutput += standardOutputChunk;
        result.standardError += standardErrorChunk;
        if (outputReceived
            && (!standardOutputChunk.isEmpty() || !standardErrorChunk.isEmpty())) {
            outputReceived(standardOutputChunk, standardErrorChunk);
        }
    };

    while (process.state() != QProcess::NotRunning) {
        if (cancelRequested && cancelRequested()) {
            stopProcess(process);
            result.termination = ProcessTermination::Cancelled;
            result.error = QStringLiteral("Executable \"%1\" was cancelled.").arg(executable);
            break;
        }
        if (spec.timeoutMs > 0 && timer.elapsed() >= spec.timeoutMs) {
            stopProcess(process);
            result.termination = ProcessTermination::TimedOut;
            result.error = QStringLiteral("Executable \"%1\" exceeded its %2 ms timeout.")
                               .arg(executable)
                               .arg(spec.timeoutMs);
            break;
        }

        qint64 waitMs = 50;
        if (spec.timeoutMs > 0) {
            waitMs = std::min(waitMs, std::max<qint64>(1, spec.timeoutMs - timer.elapsed()));
        }
        process.waitForFinished(boundedWait(waitMs));
        drainOutput();
    }

    drainOutput();
    result.exitCode = process.exitCode();
    result.elapsedMs = timer.elapsed();

    if (result.termination == ProcessTermination::NotStarted) {
        if (process.exitStatus() == QProcess::CrashExit) {
            result.termination = ProcessTermination::Crashed;
            result.error = QStringLiteral("Executable \"%1\" crashed: %2")
                               .arg(executable, process.errorString());
        } else {
            result.termination = ProcessTermination::Completed;
            if (result.exitCode != 0) {
                const QString detail = QString::fromUtf8(result.standardError).trimmed();
                result.error = detail.isEmpty()
                    ? QStringLiteral("Executable \"%1\" exited with code %2.")
                          .arg(executable)
                          .arg(result.exitCode)
                    : QStringLiteral("Executable \"%1\" exited with code %2: %3")
                          .arg(executable)
                          .arg(result.exitCode)
                          .arg(detail);
            }
        }
    }
    return result;
}

ProbeResult probeMedia(
    const FfmpegTools &tools,
    const QString &inputPath,
    qint64 timeoutMs,
    const std::function<bool()> &cancelRequested)
{
    ProbeResult result;
    result.media.inputPath = QFileInfo(inputPath).absoluteFilePath();

    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        result.error = QStringLiteral("The media file does not exist: %1").arg(inputPath);
        return result;
    }
    if (!tools.canProbe()) {
        result.error = tools.probeCapabilityError();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("FFprobe executable is unavailable.");
        }
        return result;
    }

    ProcessSpec spec;
    spec.program = tools.ffprobePath;
    spec.arguments = buildProbeArguments(inputInfo.absoluteFilePath());
    spec.timeoutMs = timeoutMs;
    result.process = runProcess(spec, cancelRequested);
    if (!result.process.succeeded()) {
        result.error = result.process.error.isEmpty()
            ? QStringLiteral("FFprobe executable \"%1\" failed.")
                  .arg(executableDisplayPath(tools.ffprobePath))
            : result.process.error;
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.process.standardOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("FFprobe executable \"%1\" returned invalid JSON: %2")
                           .arg(
                               executableDisplayPath(tools.ffprobePath),
                               parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    result.media.durationMs = secondsToMilliseconds(format.value(QStringLiteral("duration")));
    const qint64 startTime = secondsToMilliseconds(format.value(QStringLiteral("start_time")));
    result.media.startTimeMs = std::max<qint64>(0, startTime);
    result.media.formatName = format.value(QStringLiteral("format_name")).toString();

    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("video") && !result.media.hasVideo) {
            result.media.hasVideo = true;
            result.media.videoCodec = stream.value(QStringLiteral("codec_name")).toString();
            result.media.videoSize = QSize(
                stream.value(QStringLiteral("width")).toInt(),
                stream.value(QStringLiteral("height")).toInt());
            result.media.frameRate = parseFrameRate(
                stream.value(QStringLiteral("avg_frame_rate")).toString());
            if (result.media.frameRate <= 0.0) {
                result.media.frameRate = parseFrameRate(
                    stream.value(QStringLiteral("r_frame_rate")).toString());
            }
        } else if (type == QStringLiteral("audio") && !result.media.hasAudio) {
            result.media.hasAudio = true;
            result.media.audioCodec = stream.value(QStringLiteral("codec_name")).toString();
        }
    }

    if (result.media.durationMs < 0) {
        for (const QJsonValue &value : streams) {
            const qint64 duration = secondsToMilliseconds(
                value.toObject().value(QStringLiteral("duration")));
            result.media.durationMs = std::max(result.media.durationMs, duration);
        }
    }
    if (!result.media.isValid()) {
        result.error = result.media.durationMs < 0
            ? QStringLiteral("FFprobe executable \"%1\" did not report a valid media duration.")
                  .arg(executableDisplayPath(tools.ffprobePath))
            : QStringLiteral("FFprobe executable \"%1\" did not report an audio or video stream.")
                  .arg(executableDisplayPath(tools.ffprobePath));
        return result;
    }

    result.success = true;
    return result;
}

} // namespace billfish::trim
