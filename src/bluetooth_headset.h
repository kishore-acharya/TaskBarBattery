#pragma once

#include "battery_device.h"
#include "bluetooth_battery.h"

#include <string>

namespace trayring {

// A Bluetooth Classic headset whose battery Windows receives over the Hands-Free profile.
// Read-only: it only queries device properties Windows already holds; nothing is sent to the headset.
class BluetoothHeadset final : public BatteryDevice {
public:
    // hardwareId identifies the headset in its Bluetooth instance IDs, e.g. L"vid&0002054c_pid&0eaf".
    BluetoothHeadset(std::wstring name, std::wstring hardwareId);

    [[nodiscard]] std::wstring_view name() const override { return name_; }
    [[nodiscard]] DeviceKind kind() const override { return DeviceKind::Headset; }
    [[nodiscard]] BatteryReading read() override { return bluetooth::interpret(query()); }
    void reset() override {}
    [[nodiscard]] bool ownsInterface(std::wstring_view lowerCasePath) const override;
    // HFP has no charging indicator, and these headsets disconnect while charging.
    [[nodiscard]] bool reportsCharging() const override { return true; }

    [[nodiscard]] bluetooth::HandsFreeState query() const;

private:
    std::wstring name_;
    std::wstring hardwareId_;  // lower case
};

}  // namespace trayring
