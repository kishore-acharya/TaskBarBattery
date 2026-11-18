#include "battery_device.h"

#include <format>

namespace trayring {

std::wstring describe(const BatteryReading& reading) {
    switch (reading.state) {
        case LinkState::Online:
            return reading.charging ? std::format(L"Battery {}%, charging", reading.percent)
                                    : std::format(L"Battery {}%", reading.percent);
        case LinkState::DeviceOffline:
        case LinkState::ReceiverNotFound:
            return reading.detail;
        case LinkState::Error:
            return std::format(L"Battery unavailable: {}", reading.detail);
    }
    return {};
}

}  // namespace trayring
