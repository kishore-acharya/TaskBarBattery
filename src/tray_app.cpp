#include "tray_app.h"

#include "autostart.h"
#include "charge_tracker.h"
#include "logger.h"
#include "tray_icon.h"

#include <dbt.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <format>
#include <string>
#include <utility>

namespace trayring {

namespace {

constexpr auto kWindowClass = L"TrayRingWindow";
constexpr auto kAppName = L"TrayRing";

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_READING = WM_APP + 2;

constexpr UINT_PTR kDeviceChangeTimer = 1;
constexpr UINT_PTR kAddIconRetryTimer = 2;
constexpr UINT_PTR kPulseTimer = 3;
constexpr UINT kDeviceChangeDelayMs = 1000;
constexpr UINT kAddIconRetryMs = 2000;

// A 3-second charging pulse at 10 frames per second.
constexpr int kPulseFrames = 30;
constexpr UINT kPulseIntervalMs = 100;

constexpr UINT kCmdRefresh = 100;
constexpr UINT kCmdAutostart = 101;
constexpr UINT kCmdExit = 102;

constexpr auto kPollInterval = std::chrono::seconds{30};
// Receivers can report a stale value right after a device links up; re-read shortly after.
constexpr auto kLinkUpRecheck = std::chrono::seconds{5};

const wchar_t* stateName(LinkState state) {
    switch (state) {
        case LinkState::Online: return L"online";
        case LinkState::DeviceOffline: return L"device offline";
        case LinkState::ReceiverNotFound: return L"receiver not found";
        case LinkState::Error: return L"error";
    }
    return L"?";
}

UINT iconId(std::size_t index) {
    return static_cast<UINT>(index + 1);
}

}  // namespace

BatteryPoller::BatteryPoller(const std::vector<std::unique_ptr<BatteryDevice>>& devices, HWND notifyWindow,
                             UINT notifyMessage)
    : window_(notifyWindow), message_(notifyMessage), latest_(devices.size()) {
    devices_.reserve(devices.size());
    for (const auto& device : devices) {
        devices_.push_back(device.get());
    }
    thread_ = std::jthread{[this](std::stop_token stop) { run(stop); }};
}

BatteryPoller::~BatteryPoller() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void BatteryPoller::requestRefresh(bool reopenDevices) {
    {
        std::scoped_lock lock{mutex_};
        refreshRequested_ = true;
        reopenRequested_ = reopenRequested_ || reopenDevices;
    }
    wake_.notify_one();
}

std::vector<BatteryReading> BatteryPoller::latest() const {
    std::scoped_lock lock{mutex_};
    return latest_;
}

void BatteryPoller::run(std::stop_token stop) {
    std::vector<ChargeTracker> trackers(devices_.size());
    std::vector<LinkState> previous(devices_.size(), LinkState::ReceiverNotFound);
    std::vector<bool> wasCharging(devices_.size(), false);

    while (!stop.stop_requested()) {
        bool reopen = false;
        {
            std::scoped_lock lock{mutex_};
            reopen = std::exchange(reopenRequested_, false);
            refreshRequested_ = false;
        }

        std::vector<BatteryReading> readings;
        readings.reserve(devices_.size());
        auto wait = kPollInterval;
        const auto now = ChargeTracker::Clock::now();

        for (std::size_t i = 0; i < devices_.size(); ++i) {
            auto& device = *devices_[i];
            if (reopen) {
                device.reset();
            }
            auto reading = device.read();
            const bool online = reading.state == LinkState::Online;
            if (!device.reportsCharging()) {
                const bool rising = trackers[i].update(reading, previous[i] == LinkState::Online, now);
                reading.charging = online && rising;
            }
            if (online && previous[i] != LinkState::Online) {
                wait = kLinkUpRecheck;
            }
            if (reading.state != previous[i] || reading.charging != wasCharging[i]) {
                log::write(std::format(L"{}: {} -> {}: {}", device.name(), stateName(previous[i]),
                                       stateName(reading.state), describe(reading)));
            }
            previous[i] = reading.state;
            wasCharging[i] = reading.charging;
            readings.push_back(std::move(reading));
        }

        {
            std::scoped_lock lock{mutex_};
            latest_ = std::move(readings);
        }
        PostMessageW(window_, message_, 0, 0);

        std::unique_lock lock{mutex_};
        wake_.wait_for(lock, stop, wait, [this] { return refreshRequested_; });
    }
}

TrayApp::TrayApp(HINSTANCE instance, std::vector<std::unique_ptr<BatteryDevice>> devices)
    : instance_(instance), devices_(std::move(devices)), slots_(devices_.size()) {}

TrayApp::~TrayApp() {
    shutdown();
    for (auto& slot : slots_) {
        destroyFrames(slot);
    }
}

int TrayApp::run() {
    if (!createWindow()) {
        log::write(std::format(L"CreateWindowEx failed (error {})", GetLastError()));
        return 1;
    }

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    lightTaskbar_ = ui::taskbarUsesLightTheme();

    // All interface classes: HID collections for USB receivers, audio endpoints for Bluetooth headsets.
    // Each device's ownsInterface() filters the paths.
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    deviceNotification_ = RegisterDeviceNotificationW(window_, &filter,
                                                      DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    if (deviceNotification_ == nullptr) {
        log::write(std::format(L"RegisterDeviceNotification failed (error {})", GetLastError()));
    }

    for (auto& slot : slots_) {
        slot.reading.state = LinkState::DeviceOffline;
        slot.reading.detail = L"Reading battery\u2026";
    }
    addNotifyIcons();
    poller_.emplace(devices_, window_, WM_APP_READING);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool TrayApp::createWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayApp::windowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    // A hidden top-level window (not HWND_MESSAGE) so it receives the TaskbarCreated broadcast.
    window_ =
        CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    return window_ != nullptr;
}

LRESULT CALLBACK TrayApp::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* app = static_cast<TrayApp*>(create->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return app->handleMessage(window, message, wParam, lParam);
}

LRESULT TrayApp::handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        lightTaskbar_ = ui::taskbarUsesLightTheme();
        addNotifyIcons();
        return 0;
    }

    switch (message) {
        case WM_APP_READING:
            onReading();
            return 0;

        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case WM_CONTEXTMENU:
                    showContextMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                    break;
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    if (poller_) poller_->requestRefresh(false);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCmdRefresh:
                    if (poller_) poller_->requestRefresh(true);
                    break;
                case kCmdAutostart:
                    settings::setAutostartEnabled(!settings::isAutostartEnabled());
                    break;
                case kCmdExit:
                    shutdown();
                    break;
                default:
                    break;
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (lParam != 0 && std::wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
                if (const bool light = ui::taskbarUsesLightTheme(); light != lightTaskbar_) {
                    lightTaskbar_ = light;
                    for (std::size_t i = 0; i < slots_.size(); ++i) {
                        updateNotifyIcon(i, false);
                    }
                }
            }
            return 0;

        case WM_DEVICECHANGE:
            onDeviceChange(wParam, lParam);
            return TRUE;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC) {
                SetTimer(window_, kDeviceChangeTimer, kDeviceChangeDelayMs, nullptr);
            }
            return TRUE;

        case WM_TIMER:
            if (wParam == kPulseTimer) {
                advancePulse();
            } else if (wParam == kDeviceChangeTimer) {
                KillTimer(window_, kDeviceChangeTimer);
                if (poller_) poller_->requestRefresh(true);
            } else if (wParam == kAddIconRetryTimer) {
                KillTimer(window_, kAddIconRetryTimer);
                addNotifyIcons();
            }
            return 0;

        case WM_ENDSESSION:
            if (wParam != FALSE) {
                shutdown();
            }
            return 0;

        case WM_CLOSE:
            shutdown();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void TrayApp::addNotifyIcons() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = iconId(i);
        data.uFlags = NIF_MESSAGE;
        data.uCallbackMessage = WM_APP_TRAY;
        Shell_NotifyIconW(NIM_DELETE, &data);  // in case Explorer kept a stale entry
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            // Explorer may not be ready yet at logon; TaskbarCreated or the retry timer will re-add.
            SetTimer(window_, kAddIconRetryTimer, kAddIconRetryMs, nullptr);
            return;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
        updateNotifyIcon(i, true);
    }
}

ui::IconSpec TrayApp::iconSpec(std::size_t index) const {
    const auto& slot = slots_[index];
    ui::IconSpec spec;
    spec.kind = devices_[index]->kind();
    spec.lightTaskbar = lightTaskbar_;
    switch (slot.reading.state) {
        case LinkState::Online:
            spec.level = slot.reading.percent;
            spec.charging = slot.reading.charging;
            break;
        case LinkState::DeviceOffline:
            spec.level = slot.lastLevel;
            spec.dimmed = true;
            break;
        case LinkState::ReceiverNotFound:
        case LinkState::Error:
            spec.dimmed = true;
            break;
    }
    return spec;
}

std::wstring TrayApp::statusText(std::size_t index) const {
    const auto& slot = slots_[index];
    auto status = describe(slot.reading);
    if (slot.reading.state == LinkState::DeviceOffline && slot.lastLevel) {
        status += std::format(L"\nLast reading {}%", *slot.lastLevel);
    }
    return status;
}

void TrayApp::updateNotifyIcon(std::size_t index, bool forceRender) {
    auto& slot = slots_[index];
    const auto spec = iconSpec(index);

    std::vector<HICON> retired;
    if (forceRender || slot.spec != spec) {
        const int size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem());
        const int count = spec.charging ? kPulseFrames : 1;
        std::vector<HICON> frames;
        frames.reserve(static_cast<std::size_t>(count));
        for (int frame = 0; frame < count; ++frame) {
            auto frameSpec = spec;
            if (spec.charging) {
                frameSpec.arcOpacity = ui::pulseOpacity(frame, count);
            }
            if (HICON icon = ui::createTrayIcon(frameSpec, size); icon != nullptr) {
                frames.push_back(icon);
            }
        }
        if (frames.size() == static_cast<std::size_t>(count)) {
            retired = std::exchange(slot.frames, std::move(frames));
            slot.frame = 0;
            slot.spec = spec;
        } else {
            log::write(std::format(L"CreateIconIndirect failed (error {})", GetLastError()));
            std::ranges::for_each(frames, DestroyIcon);
        }
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = iconId(index);
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    if (!slot.frames.empty()) {
        data.uFlags |= NIF_ICON;
        data.hIcon = slot.frames[slot.frame];
    }
    const auto tip =
        std::format(L"{}\n{}", devices_[index]->name(), statusText(index)).substr(0, std::size(data.szTip) - 1);
    std::ranges::copy(tip, data.szTip);
    Shell_NotifyIconW(NIM_MODIFY, &data);

    // The shell keeps its own copy, so the previous frames can go once the new one is set.
    std::ranges::for_each(retired, DestroyIcon);
    updatePulseTimer();
}

void TrayApp::advancePulse() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.frames.size() < 2) {
            continue;
        }
        slot.frame = (slot.frame + 1) % slot.frames.size();
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = iconId(i);
        data.uFlags = NIF_ICON;
        data.hIcon = slot.frames[slot.frame];
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }
}

void TrayApp::updatePulseTimer() {
    const bool animate = std::ranges::any_of(slots_, [](const Slot& slot) { return slot.frames.size() > 1; });
    if (animate == pulsing_ || window_ == nullptr) {
        return;
    }
    pulsing_ = animate;
    if (animate) {
        SetTimer(window_, kPulseTimer, kPulseIntervalMs, nullptr);
    } else {
        KillTimer(window_, kPulseTimer);
    }
}

void TrayApp::removeNotifyIcons() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = iconId(i);
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
}

void TrayApp::destroyFrames(Slot& slot) {
    std::ranges::for_each(slot.frames, DestroyIcon);
    slot.frames.clear();
    slot.spec.reset();
}

void TrayApp::showContextMenu(POINT anchor) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto header = std::format(L"{}: {}", devices_[i]->name(), describe(slots_[i].reading));
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, header.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdRefresh, L"&Refresh now");
    AppendMenuW(menu, MF_STRING | (settings::isAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED), kCmdAutostart,
                L"&Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"E&xit");

    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(window_);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TrackPopupMenuEx(menu, align | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, anchor.x, anchor.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void TrayApp::onReading() {
    if (!poller_) {
        return;
    }
    auto readings = poller_->latest();
    for (std::size_t i = 0; i < slots_.size() && i < readings.size(); ++i) {
        auto& slot = slots_[i];
        slot.reading = std::move(readings[i]);
        if (slot.reading.state == LinkState::Online) {
            slot.lastLevel = slot.reading.percent;
        } else if (slot.reading.state == LinkState::ReceiverNotFound) {
            slot.lastLevel.reset();
        }
        updateNotifyIcon(i, false);
    }
}

void TrayApp::onDeviceChange(WPARAM event, LPARAM data) {
    if (event != DBT_DEVICEARRIVAL && event != DBT_DEVICEREMOVECOMPLETE) {
        return;
    }
    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if (header == nullptr || header->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
        return;
    }
    std::wstring path = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(header)->dbcc_name;
    std::ranges::transform(path, path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    for (const auto& device : devices_) {
        if (device->ownsInterface(path)) {
            log::write(std::format(L"{}: interface {}", device->name(),
                                   event == DBT_DEVICEARRIVAL ? L"arrived" : L"removed"));
            // Receivers expose several collections; one timer coalesces their notifications.
            SetTimer(window_, kDeviceChangeTimer, kDeviceChangeDelayMs, nullptr);
            return;
        }
    }
}

void TrayApp::shutdown() {
    poller_.reset();
    if (window_ == nullptr) {
        return;
    }
    removeNotifyIcons();
    if (deviceNotification_ != nullptr) {
        UnregisterDeviceNotification(deviceNotification_);
        deviceNotification_ = nullptr;
    }
    KillTimer(window_, kDeviceChangeTimer);
    KillTimer(window_, kAddIconRetryTimer);
    KillTimer(window_, kPulseTimer);
    pulsing_ = false;
    HWND window = std::exchange(window_, nullptr);
    DestroyWindow(window);
}

}  // namespace trayring
