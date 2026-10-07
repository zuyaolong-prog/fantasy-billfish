#include "windows_file_clipboard.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>

#include <cstring>
#include <limits>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <shlobj_core.h>
#endif

namespace billfish::trim {
namespace {

QStringList validateAndNormalizeFiles(const QStringList &paths, QString *error)
{
    if (paths.isEmpty()) {
        *error = QStringLiteral("At least one file is required for CF_HDROP.");
        return {};
    }

    QStringList normalized;
    QSet<QString> seen;
    for (const QString &path : paths) {
        if (path.isEmpty() || path.contains(QChar::Null) || !QDir::isAbsolutePath(path)) {
            *error = QStringLiteral("CF_HDROP requires an absolute path: %1").arg(path);
            return {};
        }
        const QFileInfo info(QDir::cleanPath(path));
        if (!info.exists() || !info.isFile()) {
            *error = QStringLiteral("CF_HDROP file does not exist: %1").arg(path);
            return {};
        }
        const QString absolute = info.absoluteFilePath();
#ifdef Q_OS_WIN
        const QString key = absolute.toCaseFolded();
#else
        const QString key = absolute;
#endif
        if (!seen.contains(key)) {
            seen.insert(key);
            normalized.append(QDir::toNativeSeparators(absolute));
        }
    }
    return normalized;
}

#ifdef Q_OS_WIN

QString windowsError(const QString &operation)
{
    const DWORD code = GetLastError();
    if (code == ERROR_SUCCESS) {
        return operation + QStringLiteral(" failed.");
    }
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t *>(&buffer),
        0,
        nullptr);
    QString detail = length > 0 && buffer
        ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
        : QStringLiteral("Win32 error %1").arg(code);
    if (buffer) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1 failed: %2").arg(operation, detail);
}

HGLOBAL allocateGlobal(const QByteArray &payload, QString *error)
{
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, static_cast<SIZE_T>(payload.size()));
    if (!memory) {
        *error = windowsError(QStringLiteral("GlobalAlloc"));
        return nullptr;
    }
    void *target = GlobalLock(memory);
    if (!target) {
        *error = windowsError(QStringLiteral("GlobalLock"));
        GlobalFree(memory);
        return nullptr;
    }
    std::memcpy(target, payload.constData(), static_cast<size_t>(payload.size()));
    GlobalUnlock(memory);
    return memory;
}

bool isValidEffect(DropEffect effect)
{
    return effect == DropEffect::Copy || effect == DropEffect::Move || effect == DropEffect::Link;
}

class ClipboardOwnerWindow final {
public:
    explicit ClipboardOwnerWindow(HWND suppliedWindow)
        : window_(suppliedWindow)
    {
        if (window_) {
            return;
        }

        constexpr wchar_t className[] = L"BillfishVideoTrimClipboardOwner";
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        window_ = CreateWindowExW(
            0,
            className,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            instance,
            nullptr);
        ownsWindow_ = window_ != nullptr;
    }

    ~ClipboardOwnerWindow()
    {
        if (ownsWindow_) {
            DestroyWindow(window_);
        }
    }

    ClipboardOwnerWindow(const ClipboardOwnerWindow &) = delete;
    ClipboardOwnerWindow &operator=(const ClipboardOwnerWindow &) = delete;

    [[nodiscard]] HWND handle() const noexcept
    {
        return window_;
    }

private:
    HWND window_ = nullptr;
    bool ownsWindow_ = false;
};

#endif

} // namespace

QByteArray WindowsFileClipboard::buildHDropPayload(
    const QStringList &absoluteFilePaths,
    QString *error)
{
    QString localError;
    QString &reportedError = error ? *error : localError;
    reportedError.clear();
    const QStringList files = validateAndNormalizeFiles(absoluteFilePaths, &reportedError);
    if (!reportedError.isEmpty()) {
        return {};
    }

#ifdef Q_OS_WIN
    quint64 utf16CodeUnits = 1; // Final list terminator.
    for (const QString &file : files) {
        utf16CodeUnits += static_cast<quint64>(file.size()) + 1U;
    }
    const quint64 totalBytes = sizeof(DROPFILES) + utf16CodeUnits * sizeof(char16_t);
    if (totalBytes > static_cast<quint64>(std::numeric_limits<int>::max())) {
        reportedError = QStringLiteral("The CF_HDROP payload is too large.");
        return {};
    }

    QByteArray payload(static_cast<int>(totalBytes), '\0');
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    std::memcpy(payload.data(), &header, sizeof(header));

    char *cursor = payload.data() + sizeof(DROPFILES);
    for (const QString &file : files) {
        const size_t bytes = static_cast<size_t>(file.size()) * sizeof(char16_t);
        std::memcpy(cursor, file.utf16(), bytes);
        cursor += bytes + sizeof(char16_t);
    }
    return payload;
#else
    Q_UNUSED(files);
    reportedError = QStringLiteral("CF_HDROP is only available on Windows.");
    return {};
#endif
}

QByteArray WindowsFileClipboard::buildPreferredDropEffectPayload(DropEffect effect)
{
    const quint32 value = static_cast<quint32>(effect);
    QByteArray payload(static_cast<int>(sizeof(value)), Qt::Uninitialized);
    std::memcpy(payload.data(), &value, sizeof(value));
    return payload;
}

ClipboardResult WindowsFileClipboard::setFiles(
    const QStringList &absoluteFilePaths,
    DropEffect effect,
    quintptr ownerWindow)
{
    ClipboardResult result;
#ifdef Q_OS_WIN
    if (!isValidEffect(effect)) {
        result.error = QStringLiteral("The Preferred DropEffect value is invalid.");
        return result;
    }
    const QByteArray hDropPayload = buildHDropPayload(absoluteFilePaths, &result.error);
    if (hDropPayload.isEmpty()) {
        return result;
    }
    const QByteArray effectPayload = buildPreferredDropEffectPayload(effect);

    HGLOBAL hDrop = allocateGlobal(hDropPayload, &result.error);
    if (!hDrop) {
        return result;
    }
    HGLOBAL preferredEffect = allocateGlobal(effectPayload, &result.error);
    if (!preferredEffect) {
        GlobalFree(hDrop);
        return result;
    }

    const UINT preferredEffectFormat = RegisterClipboardFormatW(L"Preferred DropEffect");
    if (preferredEffectFormat == 0) {
        result.error = windowsError(QStringLiteral("RegisterClipboardFormatW"));
        GlobalFree(preferredEffect);
        GlobalFree(hDrop);
        return result;
    }

    ClipboardOwnerWindow clipboardOwner(reinterpret_cast<HWND>(ownerWindow));
    if (!clipboardOwner.handle()) {
        result.error = windowsError(QStringLiteral("CreateWindowExW(clipboard owner)"));
        GlobalFree(preferredEffect);
        GlobalFree(hDrop);
        return result;
    }

    bool clipboardOpen = false;
    for (int attempt = 0; attempt < 10 && !clipboardOpen; ++attempt) {
        clipboardOpen = OpenClipboard(clipboardOwner.handle()) != FALSE;
        if (!clipboardOpen) {
            Sleep(25);
        }
    }
    if (!clipboardOpen) {
        result.error = windowsError(QStringLiteral("OpenClipboard"));
        GlobalFree(preferredEffect);
        GlobalFree(hDrop);
        return result;
    }

    if (!EmptyClipboard()) {
        result.error = windowsError(QStringLiteral("EmptyClipboard"));
        CloseClipboard();
        GlobalFree(preferredEffect);
        GlobalFree(hDrop);
        return result;
    }
    if (!SetClipboardData(CF_HDROP, hDrop)) {
        result.error = windowsError(QStringLiteral("SetClipboardData(CF_HDROP)"));
        CloseClipboard();
        GlobalFree(preferredEffect);
        GlobalFree(hDrop);
        return result;
    }
    hDrop = nullptr; // Clipboard owns this allocation now.

    if (!SetClipboardData(preferredEffectFormat, preferredEffect)) {
        result.error = windowsError(QStringLiteral("SetClipboardData(Preferred DropEffect)"));
        EmptyClipboard();
        CloseClipboard();
        GlobalFree(preferredEffect);
        return result;
    }
    preferredEffect = nullptr; // Clipboard owns this allocation now.
    CloseClipboard();
    result.success = true;
    return result;
#else
    Q_UNUSED(absoluteFilePaths);
    Q_UNUSED(effect);
    Q_UNUSED(ownerWindow);
    result.error = QStringLiteral("Windows file clipboard support is only available on Windows.");
    return result;
#endif
}

} // namespace billfish::trim
