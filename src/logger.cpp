#include "logger.h"

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <mutex>

namespace trayring::log {

namespace {

std::mutex gMutex;
std::FILE* gFile = nullptr;

}  // namespace

void enable(const std::filesystem::path& file) {
    std::scoped_lock lock{gMutex};
    if (gFile != nullptr) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Shared for reading so the log can be inspected while the app runs.
    gFile = _wfsopen(file.c_str(), L"a, ccs=UTF-8", _SH_DENYWR);
}

bool enabled() noexcept {
    std::scoped_lock lock{gMutex};
    return gFile != nullptr;
}

void write(std::wstring_view message) {
    std::scoped_lock lock{gMutex};
    if (gFile == nullptr) {
        return;
    }
    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::fwprintf(gFile, L"%04u-%02u-%02u %02u:%02u:%02u.%03u %.*ls\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond, t.wMilliseconds, static_cast<int>(message.size()), message.data());
    std::fflush(gFile);
}

}  // namespace trayring::log
