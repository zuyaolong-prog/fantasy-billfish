#include "ui/player_trim_injector.hpp"
#include "automation/runtime_host.hpp"
#include "automation/native_adapter.hpp"
#include "shell/compact_shell.hpp"
#include "shell/legacy_feature_gate.hpp"

#include <QObject>
#include <QCoreApplication>
#include <QtGlobal>

#include <Windows.h>

#include <mutex>
#include <string>

namespace {

constexpr wchar_t kStockPluginName[] = L"preview.stock.bin";

// QPluginLoader inspects this PE section before it loads a dynamic plugin and
// calls qt_plugin_query_metadata. Keep it byte-for-byte identical to the stock
// Billfish preview plugin metadata; the export below still forwards at runtime.
#pragma section(".qtmetad", read, discard)
__declspec(allocate(".qtmetad")) const unsigned char kStockPluginMetadata[] = {
    0x51, 0x54, 0x4D, 0x45, 0x54, 0x41, 0x44, 0x41, 0x54, 0x41, 0x20, 0x21, 0x00, 0x05, 0x0F,
    0x00, 0xBF, 0x02, 0x78, 0x22, 0x42, 0x69, 0x6C, 0x6C, 0x66, 0x69, 0x73, 0x68, 0x2E, 0x50,
    0x6C, 0x75, 0x67, 0x69, 0x6E, 0x2E, 0x49, 0x50, 0x72, 0x65, 0x76, 0x69, 0x65, 0x77, 0x50,
    0x6C, 0x75, 0x67, 0x69, 0x6E, 0x2F, 0x31, 0x2E, 0x30, 0x03, 0x6D, 0x50, 0x72, 0x65, 0x76,
    0x69, 0x65, 0x77, 0x50, 0x6C, 0x75, 0x67, 0x69, 0x6E, 0x04, 0xA1, 0x64, 0x4B, 0x65, 0x79,
    0x73, 0x81, 0x6B, 0x70, 0x6C, 0x67, 0x5F, 0x70, 0x72, 0x65, 0x76, 0x69, 0x65, 0x77, 0xFF,
};

using QueryMetadataFunction = const char* (*)();
using PluginInstanceFunction = QObject* (*)();

HMODULE g_proxyModule = nullptr;

struct StockPlugin final {
    HMODULE module = nullptr;
    QueryMetadataFunction queryMetadata = nullptr;
    PluginInstanceFunction pluginInstance = nullptr;
};

void debugError(const wchar_t* message, const DWORD error) noexcept {
    wchar_t buffer[320] = {};
    const int count = swprintf_s(buffer, L"[BillfishVideoTrim] %s (Win32 error %lu)\n", message, error);
    if (count > 0) {
        OutputDebugStringW(buffer);
    }
}

std::wstring proxyDirectory() {
    if (g_proxyModule == nullptr) {
        return {};
    }
    std::wstring path(512, L'\0');
    for (;;) {
        const DWORD copied = GetModuleFileNameW(g_proxyModule, path.data(), static_cast<DWORD>(path.size()));
        if (copied == 0) {
            debugError(L"GetModuleFileNameW failed", GetLastError());
            return {};
        }
        if (copied < path.size() - 1) {
            path.resize(copied);
            break;
        }
        if (path.size() >= 32768) {
            debugError(L"proxy path is too long", ERROR_BUFFER_OVERFLOW);
            return {};
        }
        path.resize(path.size() * 2);
    }
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }
    path.resize(separator);
    return path;
}

StockPlugin loadStockPlugin() noexcept {
    StockPlugin stock;
    const std::wstring directory = proxyDirectory();
    if (directory.empty()) {
        return stock;
    }
    const std::wstring path = directory + L"\\" + kStockPluginName;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        debugError(L"preview.stock.bin is missing", GetLastError());
        return stock;
    }

    constexpr DWORD safeSearchFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    stock.module = LoadLibraryExW(path.c_str(), nullptr, safeSearchFlags);
    if (stock.module == nullptr) {
        debugError(L"safe loading preview.stock.bin failed", GetLastError());
        return stock;
    }
    stock.queryMetadata = reinterpret_cast<QueryMetadataFunction>(
        GetProcAddress(stock.module, "qt_plugin_query_metadata"));
    stock.pluginInstance = reinterpret_cast<PluginInstanceFunction>(
        GetProcAddress(stock.module, "qt_plugin_instance"));
    if (stock.queryMetadata == nullptr || stock.pluginInstance == nullptr) {
        debugError(L"preview.stock.bin has an invalid Qt plugin export contract", ERROR_PROC_NOT_FOUND);
        stock.queryMetadata = nullptr;
        stock.pluginInstance = nullptr;
    }
    return stock;
}

const StockPlugin& stockPlugin() noexcept {
    static std::once_flag once;
    static StockPlugin stock;
    std::call_once(once, [] { stock = loadStockPlugin(); });
    return stock;
}

void pinProxyModule() noexcept {
    const std::wstring directory = proxyDirectory();
    if (directory.empty()) {
        return;
    }
    const std::wstring path = directory + L"\\preview.dll";
    HMODULE pinnedModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, path.c_str(), &pinnedModule)) {
        debugError(L"pinning preview proxy failed", GetLastError());
    }
}

}  // namespace

extern "C" Q_DECL_EXPORT const char* qt_plugin_query_metadata() {
    const StockPlugin& stock = stockPlugin();
    return stock.queryMetadata != nullptr
               ? stock.queryMetadata()
               : reinterpret_cast<const char*>(kStockPluginMetadata);
}

extern "C" Q_DECL_EXPORT QObject* qt_plugin_instance() {
    const StockPlugin& stock = stockPlugin();
    if (stock.pluginInstance == nullptr) {
        return nullptr;
    }
    QObject* instance = stock.pluginInstance();
    if (instance != nullptr) {
        pinProxyModule();
        billfish::automation::installAutomationInjection(instance);
        billfish::trim::installPlayerTrimInjection(instance, [] {
            auto* application = QCoreApplication::instance();
            auto* adapter = application ? application->findChild<billfish::automation::NativeAdapter*>() : nullptr;
            if (!adapter) return QString();
            const QJsonObject info = adapter->info();
            return info.value(QStringLiteral("ready")).toBool()
                ? info.value(QStringLiteral("library_root")).toString() : QString();
        });
        billfish::shell::installCompactShell();
        billfish::shell::installLegacyFeatureGate();
    }
    return instance;
}

BOOL APIENTRY DllMain(HMODULE module, const DWORD reason, LPVOID reserved) {
    Q_UNUSED(reserved)
    if (reason == DLL_PROCESS_ATTACH) {
        g_proxyModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
