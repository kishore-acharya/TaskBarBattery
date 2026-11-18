#pragma once

#include <string>
#include <string_view>

namespace trayring {

enum class DeviceKind { Keyboard, Mouse, Headset };

enum class LinkState {
    ReceiverNotFound,  // the device's receiver/cable is not present
    DeviceOffline,     // receiver answers, device not linked (off, asleep, other mode)
    Online,            // device linked, battery valid
    Error,             // receiver present but the exchange failed or returned nonsense
};

struct BatteryReading {
    LinkState state{LinkState::ReceiverNotFound};
    int percent{-1};
    bool charging{false};
    std::wstring detail;
};

// A battery-powered peripheral the tray shows one icon for. Implementations are polled from a
// single background thread; they must never block indefinitely or alter device configuration.
class BatteryDevice {
public:
    virtual ~BatteryDevice() = default;

    [[nodiscard]] virtual std::wstring_view name() const = 0;
    [[nodiscard]] virtual DeviceKind kind() const = 0;
    [[nodiscard]] virtual BatteryReading read() = 0;

    // Drops cached handles, e.g. after a device arrival/removal notification.
    virtual void reset() = 0;

    // Whether a device interface path (lower case) that arrived or left belongs to this device.
    [[nodiscard]] virtual bool ownsInterface(std::wstring_view lowerCasePath) const = 0;

    // True when read() reports charging itself; otherwise charging is inferred from a rising level.
    [[nodiscard]] virtual bool reportsCharging() const { return false; }
};

[[nodiscard]] std::wstring describe(const BatteryReading& reading);

}  // namespace trayring
