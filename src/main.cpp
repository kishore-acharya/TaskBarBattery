#include "autostart.h"
#include "bluetooth_headset.h"
#include "ronin_keyboard.h"
#include "logger.h"
#include "razer_mouse.h"
#include "tray_app.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path logFilePath() {
    PWSTR folder = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder))) {
        path = std::filesystem::path{folder} / L"TrayRing" / L"trayring.log";
    }
    CoTaskMemFree(folder);
    return path;
}

bool hasArgument(std::wstring_view expected) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; argv != nullptr && i < argc; ++i) {
        found = found || expected == argv[i];
    }
    LocalFree(argv);
    return found;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\TrayRing.SingleInstance");
    if (singleInstance == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singleInstance != nullptr) {
            CloseHandle(singleInstance);
        }
        return 0;
    }

    if (hasArgument(L"--debug") || trayring::settings::isDebugLogEnabled()) {
        if (const auto path = logFilePath(); !path.empty()) {
            trayring::log::enable(path);
        }
    }
    trayring::log::write(L"Starting");

    trayring::settings::initializeAutostart();

    int result = 0;
    {
        // Add further BatteryDevice implementations here; each gets its own tray icon.
        std::vector<std::unique_ptr<trayring::BatteryDevice>> devices;
        devices.push_back(std::make_unique<trayring::RoninKeyboard>());
        devices.push_back(std::make_unique<trayring::RazerDeathAdderV2X>());
        devices.push_back(std::make_unique<trayring::BluetoothHeadset>(L"Sony WH-CH720N", L"vid&0002054c_pid&0eaf"));

        trayring::TrayApp app{instance, std::move(devices)};
        result = app.run();
    }

    trayring::log::write(L"Exiting");
    CloseHandle(singleInstance);
    return result;
}
