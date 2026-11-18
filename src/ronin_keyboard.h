#pragma once

#include "battery_device.h"
#include "ronin_protocol.h"
#include "hid_device.h"

#include <optional>
#include <string>

namespace trayring {

// A battery reading plus the decoded and raw 0xF7 reply, for diagnostics.
struct ReceiverReading : BatteryReading {
    std::optional<ronin::ReceiverStatus> status;
    std::optional<ronin::Report> rawReply;
};

// Owns the receiver's vendor collection handle and performs the 0xF7 status exchange.
// Not thread-safe; use from one thread.
class BatteryMonitor {
public:
    [[nodiscard]] ReceiverReading read();

    // Drops the cached handle, e.g. after a device removal notification.
    void reset() noexcept { device_.reset(); }

private:
    struct Exchange {
        DWORD error{ERROR_SUCCESS};
        ronin::Report reply{};
    };

    [[nodiscard]] bool ensureOpen(ReceiverReading& reading);
    [[nodiscard]] Exchange exchange() const;
    [[nodiscard]] static ReceiverReading interpret(const ronin::Report& reply);

    std::optional<hid::Device> device_;
};

// EvoFox Ronin X75 Plus over its 3151:4011 2.4 GHz receiver.
class RoninKeyboard final : public BatteryDevice {
public:
    [[nodiscard]] std::wstring_view name() const override { return L"EvoFox Ronin X75 Plus"; }
    [[nodiscard]] DeviceKind kind() const override { return DeviceKind::Keyboard; }
    [[nodiscard]] BatteryReading read() override { return monitor_.read(); }
    void reset() override { monitor_.reset(); }
    [[nodiscard]] bool ownsInterface(std::wstring_view lowerCasePath) const override;

private:
    BatteryMonitor monitor_;
};

// The receiver's battery-capable collection: vendor page 0xFFFF with a 64-byte feature report.
[[nodiscard]] bool isStatusInterface(const hid::InterfaceInfo& info) noexcept;

}  // namespace trayring
