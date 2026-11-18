#include "autostart.h"

#include "logger.h"

#include <windows.h>

#include <array>
#include <format>
#include <string>

namespace trayring::settings {

namespace {

constexpr auto kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
// Task Manager's "Startup apps" toggle; an odd first byte means the user disabled the entry.
constexpr auto kStartupApprovedKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr auto kSettingsKey = L"Software\\TrayRing";
constexpr auto kValueName = L"TrayRing";

std::wstring executablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring runCommand() {
    return std::format(L"\"{}\"", executablePath());
}

std::wstring readRunValue() {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) !=
            ERROR_SUCCESS ||
        bytes == 0) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) !=
        ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

bool disabledInTaskManager() {
    std::array<BYTE, 12> data{};
    DWORD bytes = static_cast<DWORD>(data.size());
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kValueName, RRF_RT_REG_BINARY, nullptr, data.data(),
                     &bytes) != ERROR_SUCCESS ||
        bytes == 0) {
        return false;
    }
    return (data[0] & 1) != 0;
}

DWORD readSettingDword(const wchar_t* name, DWORD fallback) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) !=
        ERROR_SUCCESS) {
        return fallback;
    }
    return value;
}

void writeSettingDword(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &value, sizeof(value));
}

bool writeRunValue() {
    const auto command = runCommand();
    const auto status =
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(),
                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        log::write(std::format(L"Cannot write Run entry (error {})", status));
        return false;
    }
    return true;
}

}  // namespace

bool isAutostartEnabled() {
    return !readRunValue().empty() && !disabledInTaskManager();
}

bool setAutostartEnabled(bool enabled) {
    if (enabled) {
        if (!writeRunValue()) {
            return false;
        }
        // Clear a Task Manager "disabled" flag so the entry actually runs.
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kValueName);
        return true;
    }
    const auto status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

void initializeAutostart() {
    if (readSettingDword(L"AutostartConfigured", 0) == 0) {
        if (setAutostartEnabled(true)) {
            writeSettingDword(L"AutostartConfigured", 1);
        }
        return;
    }
    const auto current = readRunValue();
    if (!current.empty() && _wcsicmp(current.c_str(), runCommand().c_str()) != 0) {
        writeRunValue();
    }
}

bool isDebugLogEnabled() {
    return readSettingDword(L"DebugLog", 0) != 0;
}

}  // namespace trayring::settings
