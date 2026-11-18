#include "bluetooth_battery.h"

#include <format>

namespace trayring::bluetooth {

BatteryReading interpret(const HandsFreeState& state) {
    BatteryReading reading;
    if (!state.paired) {
        reading.state = LinkState::ReceiverNotFound;
        reading.detail = L"Not paired with this PC";
    } else if (!state.connected) {
        reading.state = LinkState::DeviceOffline;
        reading.detail = L"Disconnected";
    } else if (!state.battery) {
        reading.state = LinkState::Error;
        reading.detail = L"Battery level not reported yet";
    } else if (*state.battery > 100) {
        reading.state = LinkState::Error;
        reading.detail = std::format(L"Invalid battery value {}", *state.battery);
    } else {
        reading.state = LinkState::Online;
        reading.percent = *state.battery;
        reading.detail = std::format(L"Battery {}%", reading.percent);
    }
    return reading;
}

}  // namespace trayring::bluetooth
