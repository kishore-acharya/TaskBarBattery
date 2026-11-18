#pragma once

// Battery of a Bluetooth Classic headset as reported by Windows' Hands-Free (HFP) audio gateway.
// Pure data handling: no Windows dependencies.

#include "battery_device.h"

#include <cstdint>
#include <optional>

namespace trayring::bluetooth {

struct HandsFreeState {
    bool paired{};                       // the device's Hands-Free AG node exists
    bool connected{};                    // the device node's "connected" property
    std::optional<std::uint8_t> battery;  // the AG node's battery property, percent
};

// Windows keeps the last battery value after a disconnect, so the level counts only while connected.
[[nodiscard]] BatteryReading interpret(const HandsFreeState& state);

}  // namespace trayring::bluetooth
