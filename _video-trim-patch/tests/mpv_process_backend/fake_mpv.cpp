#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::wstring environmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    value.resize(written);
    return value;
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0,
                                             nullptr, nullptr);
    std::string result(static_cast<std::size_t>(required), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                              result.data(), required, nullptr, nullptr);
    return result;
}

void appendLog(const std::wstring& logPath, const std::string_view kind,
               const std::string_view value) {
    if (logPath.empty()) {
        return;
    }
    std::ofstream stream(std::filesystem::path(logPath), std::ios::binary | std::ios::app);
    stream << kind << '\t' << value << '\n';
}

bool writeAll(const HANDLE pipe, const std::string_view payload) {
    std::size_t offset = 0;
    while (offset < payload.size()) {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(
            std::min<std::size_t>(payload.size() - offset, MAXDWORD));
        if (!WriteFile(pipe, payload.data() + offset, remaining, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    (void)FlushFileBuffers(pipe);
    return true;
}

std::string requestId(const std::string_view request) {
    constexpr std::string_view marker = "\"request_id\":";
    const std::size_t markerPosition = request.find(marker);
    if (markerPosition == std::string_view::npos) {
        return "0";
    }
    std::size_t begin = markerPosition + marker.size();
    while (begin < request.size() && request[begin] == ' ') {
        ++begin;
    }
    std::size_t end = begin;
    while (end < request.size() && request[end] >= '0' && request[end] <= '9') {
        ++end;
    }
    return end > begin ? std::string(request.substr(begin, end - begin)) : "0";
}

bool contains(const std::string_view value, const std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

bool respondToCommand(const HANDLE pipe, const std::string_view request,
                      const std::wstring& mode) {
    const std::string response =
        "{\"request_id\":" + requestId(request) + ",\"error\":\"success\"}\n";
    if (!writeAll(pipe, response)) {
        return false;
    }

    if (contains(request, "\"observe_property\"") && contains(request, "\"time-pos\"")) {
        return writeAll(pipe,
                        "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\",\"data\":0.0}\n");
    }
    if (contains(request, "\"observe_property\"") && contains(request, "\"duration\"")) {
        return writeAll(pipe,
                        "{\"event\":\"property-change\",\"id\":2,\"name\":\"duration\",\"data\":12.345}\n");
    }
    if (contains(request, "\"observe_property\"") && contains(request, "\"pause\"")) {
        return writeAll(pipe,
                        "{\"event\":\"property-change\",\"id\":3,\"name\":\"pause\",\"data\":true}\n");
    }
    if (contains(request, "\"loadfile\"")) {
        return writeAll(pipe, "{\"event\":\"file-loaded\"}\n");
    }
    if (contains(request, "\"seek\"")) {
        return writeAll(pipe,
                        "{\"event\":\"property-change\",\"id\":1,\"name\":\"time-pos\",\"data\":4.321}\n");
    }
    if (contains(request, "\"set_property\",\"pause\",false")) {
        return writeAll(pipe,
                        "{\"event\":\"property-change\",\"id\":3,\"name\":\"pause\",\"data\":false}\n");
    }
    if (contains(request, "\"set_property\",\"pause\",true")) {
        return writeAll(pipe,
                        "{\"event\":\"property-change\",\"id\":3,\"name\":\"pause\",\"data\":true}\n");
    }
    if (contains(request, "\"quit\"") && mode != L"ignore-quit") {
        return false;
    }
    return true;
}

}  // namespace

int wmain(const int argc, wchar_t** argv) {
    std::wstring pipePath;
    const std::wstring logPath = environmentValue(L"BILLFISH_FAKE_MPV_LOG");
    const std::wstring mode = environmentValue(L"BILLFISH_FAKE_MPV_MODE");
    constexpr std::wstring_view pipePrefix = L"--input-ipc-server=";
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        appendLog(logPath, "ARG", utf8(std::wstring(argument)));
        if (argument.substr(0, pipePrefix.size()) == pipePrefix) {
            pipePath.assign(argument.substr(pipePrefix.size()));
        }
    }
    if (pipePath.empty()) {
        return 2;
    }

    const HANDLE pipe = CreateNamedPipeW(
        pipePath.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 64 * 1024, 64 * 1024, 0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return 3;
    }
    const BOOL connected = ConnectNamedPipe(pipe, nullptr)
        ? TRUE
        : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);
    if (!connected) {
        CloseHandle(pipe);
        return 4;
    }

    if (mode == L"malformed") {
        (void)writeAll(pipe, "this-is-not-json\n");
    }

    std::string buffer;
    std::vector<char> chunk(4096);
    bool keepRunning = true;
    int commandsSeen = 0;
    while (keepRunning) {
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &bytesRead, nullptr) ||
            bytesRead == 0) {
            break;
        }
        buffer.append(chunk.data(), bytesRead);
        std::size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            appendLog(logPath, "CMD", line);
            ++commandsSeen;
            keepRunning = respondToCommand(pipe, line, mode);
            if (mode == L"exit-after-connect" && commandsSeen >= 3) {
                keepRunning = false;
            }
            if (!keepRunning) {
                break;
            }
        }
    }

    (void)FlushFileBuffers(pipe);
    (void)DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    return mode == L"exit-after-connect" ? 17 : 0;
}
