#pragma once

#include "battery_device.h"
#include "icon_renderer.h"

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace trayring {

// Polls every device on one background thread so a slow HID transfer never blocks the UI.
// For devices that cannot report charging, charging is inferred with a ChargeTracker.
class BatteryPoller {
public:
    BatteryPoller(const std::vector<std::unique_ptr<BatteryDevice>>& devices, HWND notifyWindow,
                  UINT notifyMessage);
    ~BatteryPoller();
    BatteryPoller(const BatteryPoller&) = delete;
    BatteryPoller& operator=(const BatteryPoller&) = delete;

    void requestRefresh(bool reopenDevices);
    [[nodiscard]] std::vector<BatteryReading> latest() const;

private:
    void run(std::stop_token stop);

    std::vector<BatteryDevice*> devices_;
    HWND window_;
    UINT message_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    bool refreshRequested_{false};
    bool reopenRequested_{false};
    std::vector<BatteryReading> latest_;
    std::jthread thread_;
};

class TrayApp {
public:
    TrayApp(HINSTANCE instance, std::vector<std::unique_ptr<BatteryDevice>> devices);
    ~TrayApp();
    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    [[nodiscard]] int run();

private:
    // One notification-area icon per device.
    struct Slot {
        BatteryReading reading;
        std::optional<int> lastLevel;
        std::optional<ui::IconSpec> spec;  // what `frames` currently show
        std::vector<HICON> frames;         // one frame, or a pulse cycle while charging
        std::size_t frame{0};
    };

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    bool createWindow();
    void addNotifyIcons();
    void updateNotifyIcon(std::size_t index, bool forceRender);
    void advancePulse();
    void updatePulseTimer();
    void removeNotifyIcons();
    void showContextMenu(POINT anchor);
    void onReading();
    void onDeviceChange(WPARAM event, LPARAM data);
    void shutdown();

    [[nodiscard]] ui::IconSpec iconSpec(std::size_t index) const;
    [[nodiscard]] std::wstring statusText(std::size_t index) const;
    static void destroyFrames(Slot& slot);

    HINSTANCE instance_;
    std::vector<std::unique_ptr<BatteryDevice>> devices_;
    std::vector<Slot> slots_;
    HWND window_{nullptr};
    HDEVNOTIFY deviceNotification_{nullptr};
    UINT taskbarCreatedMessage_{0};
    bool lightTaskbar_{false};
    bool pulsing_{false};
    std::optional<BatteryPoller> poller_;
};

}  // namespace trayring
