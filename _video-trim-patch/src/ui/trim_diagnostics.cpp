#include "ui/trim_diagnostics.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>

namespace billfish::trim {

namespace {

constexpr int kMaximumMessageCharacters = 2048;
constexpr qint64 kDuplicateDebounceMs = 1000;
constexpr qint64 kRateWindowMs = 1000;
constexpr int kMaximumRecordsPerWindow = 20;
constexpr int kMaximumRememberedMessages = 64;

struct DiagnosticState final {
    QHash<QString, qint64> recentMessages;
    qint64 rateWindowStartedMs{0};
    int recordsInWindow{0};
    quint64 suppressedRecords{0};
};

QMutex& diagnosticMutex() {
    static QMutex mutex;
    return mutex;
}

DiagnosticState& diagnosticState() {
    static DiagnosticState state;
    return state;
}

QString boundedSingleLine(QString message) {
    message.replace(QLatin1Char('\r'), QLatin1Char(' '));
    message.replace(QLatin1Char('\n'), QLatin1Char(' '));
    message.replace(QLatin1Char('\t'), QLatin1Char(' '));
    if (message.size() > kMaximumMessageCharacters) {
        message = message.left(kMaximumMessageCharacters - 3) + QStringLiteral("...");
    }
    return message;
}

QByteArray makeRecord(const QString& message, const qint64 nowMs) {
    return QStringLiteral("%1 pid=%2 %3\n")
        .arg(QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC).toString(Qt::ISODateWithMs))
        .arg(QCoreApplication::applicationPid())
        .arg(message)
        .toUtf8();
}

bool replaceWithBoundedLog(const QString& path, const QByteArray& record, const qint64 nowMs) {
    const QByteArray resetMarker = makeRecord(
        QStringLiteral("diagnostic-log-reset reason=size-limit maxBytes=%1")
            .arg(trimDiagnosticMaximumBytes()),
        nowMs);
    QByteArray replacement = resetMarker + record;
    if (replacement.size() > trimDiagnosticMaximumBytes()) {
        replacement = record.left(trimDiagnosticMaximumBytes());
    }

    QSaveFile saveFile(path);
    if (saveFile.open(QIODevice::WriteOnly) &&
        saveFile.write(replacement) == replacement.size() && saveFile.commit()) {
        return true;
    }
    saveFile.cancelWriting();

    QFile fallback(path);
    return fallback.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           fallback.write(replacement) == replacement.size() && fallback.flush();
}

}  // namespace

bool trimDiagnosticsEnabled() noexcept {
    return qgetenv("BILLFISH_TRIM_DEBUG") == QByteArrayLiteral("1");
}

QString trimDiagnosticPath() {
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) {
        localAppData = QDir::homePath() + QStringLiteral("/AppData/Local");
    }
    return QDir(localAppData).filePath(QStringLiteral("Billfish/VideoTrim/diagnostic.log"));
}

void trimDiagnostic(const QString& message) {
    if (!trimDiagnosticsEnabled()) {
        return;
    }

    QMutexLocker locker(&diagnosticMutex());
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString normalizedMessage = boundedSingleLine(message);
    DiagnosticState& state = diagnosticState();

    if (state.rateWindowStartedMs == 0 || nowMs - state.rateWindowStartedMs >= kRateWindowMs ||
        nowMs < state.rateWindowStartedMs) {
        state.rateWindowStartedMs = nowMs;
        state.recordsInWindow = 0;
    }

    for (auto iterator = state.recentMessages.begin(); iterator != state.recentMessages.end();) {
        if (nowMs < iterator.value() || nowMs - iterator.value() >= kDuplicateDebounceMs) {
            iterator = state.recentMessages.erase(iterator);
        } else {
            ++iterator;
        }
    }
    const auto recent = state.recentMessages.constFind(normalizedMessage);
    if (recent != state.recentMessages.cend() && nowMs - recent.value() < kDuplicateDebounceMs) {
        ++state.suppressedRecords;
        return;
    }
    if (state.recordsInWindow >= kMaximumRecordsPerWindow) {
        ++state.suppressedRecords;
        return;
    }
    if (state.recentMessages.size() >= kMaximumRememberedMessages) {
        state.recentMessages.clear();
    }
    state.recentMessages.insert(normalizedMessage, nowMs);

    QString recordMessage = normalizedMessage;
    if (state.suppressedRecords > 0) {
        recordMessage = QStringLiteral("suppressed=%1; %2")
                            .arg(state.suppressedRecords)
                            .arg(recordMessage);
        state.suppressedRecords = 0;
    }
    ++state.recordsInWindow;

    const QString path = trimDiagnosticPath();
    (void)QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray record = makeRecord(recordMessage, nowMs);
    const qint64 maximumBytes = trimDiagnosticMaximumBytes();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    if (file.size() > maximumBytes - record.size()) {
        file.close();
        (void)replaceWithBoundedLog(path, record, nowMs);
        return;
    }
    if (file.write(record) != record.size() || !file.flush()) {
        return;
    }
    if (file.size() > maximumBytes) {
        file.close();
        (void)replaceWithBoundedLog(path, record, nowMs);
    }
}

}  // namespace billfish::trim
