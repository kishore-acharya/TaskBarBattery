#pragma once

#include "battery_device.h"
#include "hid_device.h"
#include "razer_protocol.h"

#include <optional>
#include <string>
#include <vector>

namespace trayring {

// Battery reading plus the raw reply, for diagnostics.
struct RazerReading : BatteryReading {
    std::optional<razer::Report> rawReply;
    std::wstring interfacePath;
};

// Razer DeathAdder V2 X HyperSpeed over its 1532:009C HyperSpeed dongle.
// AA/AAA powered: OpenRazer reports it never charges, so no charging state is shown.
class RazerDeathAdderV2X final : public BatteryDevice {
public:
    [[nodiscard]] std::wstring_view name() const override { return L"Razer DeathAdder V2 X HyperSpeed"; }
    [[nodiscard]] DeviceKind kind() const override { return DeviceKind::Mouse; }
    [[nodiscard]] BatteryReading read() override { return readDetailed(); }
    void reset() override { device_.reset(); }
    [[nodiscard]] bool ownsInterface(std::wstring_view lowerCasePath) const override;
    // Charging is known (never), so a level rise after a battery swap is not shown as charging.
    [[nodiscard]] bool reportsCharging() const override { return true; }

    [[nodiscard]] RazerReading readDetailed();

private:
    struct Exchange {
        DWORD error{ERROR_SUCCESS};
        razer::Report reply{};
        razer::BatteryReply parsed{};
    };

    [[nodiscard]] bool ensureOpen(RazerReading& reading);
    [[nodiscard]] Exchange exchange() const;

    std::optional<hid::Device> device_;
};

// Collections able to carry the 90-byte control report, best candidate first.
[[nodiscard]] std::vector<hid::InterfaceInfo> razerControlInterfaces(std::uint16_t productId);

}  // namespace trayring
