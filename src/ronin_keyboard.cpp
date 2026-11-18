#include "ronin_keyboard.h"

#include "logger.h"

#include <algorithm>
#include <array>
#include <format>

namespace trayring {

namespace {

constexpr int kMaxAttempts = 3;
// Upstream measured a receiver reply ready ~10 ms after the SET; leave some margin.
constexpr DWORD kReplyDelayMs = 15;
constexpr DWORD kRetryDelayMs = 100;

ReceiverReading makeReading(LinkState state, std::wstring detail) {
    ReceiverReading reading;
    reading.state = state;
    reading.detail = std::move(detail);
    return reading;
}

}  // namespace

bool isStatusInterface(const hid::InterfaceInfo& info) noexcept {
    return info.usagePage == ronin::kVendorUsagePage &&
           info.featureReportLength == ronin::kFeatureBufferLength;
}

bool RoninKeyboard::ownsInterface(std::wstring_view lowerCasePath) const {
    return lowerCasePath.find(L"vid_3151&pid_4011") != std::wstring_view::npos;
}

bool BatteryMonitor::ensureOpen(ReceiverReading& reading) {
    if (device_) {
        return true;
    }

    const auto interfaces = hid::enumerate(ronin::kVendorId, ronin::kReceiverProductId);
    if (interfaces.empty()) {
        reading = makeReading(LinkState::ReceiverNotFound, L"2.4 GHz receiver not found");
        return false;
    }

    const auto it = std::ranges::find_if(interfaces, isStatusInterface);
    if (it == interfaces.end()) {
        reading = makeReading(LinkState::Error, L"Receiver vendor interface not found");
        log::write(reading.detail);
        return false;
    }

    DWORD error = ERROR_SUCCESS;
    device_ = hid::Device::open(it->path, error);
    if (!device_) {
        reading = makeReading(LinkState::Error, std::format(L"Cannot open receiver (error {})", error));
        log::write(std::format(L"{}: {}", reading.detail, it->path));
        return false;
    }
    log::write(std::format(L"Opened {}", it->path));
    return true;
}

BatteryMonitor::Exchange BatteryMonitor::exchange() const {
    Exchange result;

    std::array<std::uint8_t, ronin::kFeatureBufferLength> request{};
    const auto payload = ronin::buildStatusRequest();
    std::ranges::copy(payload, request.begin() + 1);
    if (result.error = device_->setFeature(request); result.error != ERROR_SUCCESS) {
        log::write(std::format(L"HidD_SetFeature failed (error {})", result.error));
        return result;
    }

    Sleep(kReplyDelayMs);

    std::array<std::uint8_t, ronin::kFeatureBufferLength> reply{};
    if (result.error = device_->getFeature(reply); result.error != ERROR_SUCCESS) {
        log::write(std::format(L"HidD_GetFeature failed (error {})", result.error));
        return result;
    }
    std::ranges::copy(reply.begin() + 1, reply.end(), result.reply.begin());
    return result;
}

ReceiverReading BatteryMonitor::interpret(const ronin::Report& reply) {
    ReceiverReading reading;
    reading.rawReply = reply;

    const auto parsed = ronin::parseStatusReply(reply);
    if (!parsed.status) {
        reading.state = LinkState::Error;
        reading.detail = L"Unrecognised receiver reply";
        return reading;
    }

    const auto& status = *parsed.status;
    reading.status = status;
    if (!status.hasKeyboard()) {
        reading.state = LinkState::DeviceOffline;
        reading.detail = L"No keyboard paired with receiver";
    } else if (!status.keyboardOnline) {
        reading.state = LinkState::DeviceOffline;
        reading.detail = L"Keyboard asleep or disconnected";
    } else if (!ronin::isValidBatteryPercent(status.keyboardBattery)) {
        reading.state = LinkState::Error;
        reading.detail = std::format(L"Invalid battery value {}", status.keyboardBattery);
    } else {
        reading.state = LinkState::Online;
        reading.percent = status.keyboardBattery;
        reading.detail = std::format(L"Battery {}%", reading.percent);
    }
    return reading;
}

ReceiverReading BatteryMonitor::read() {
    ReceiverReading reading;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (attempt > 0) {
            Sleep(kRetryDelayMs);
        }
        if (!ensureOpen(reading)) {
            if (reading.state == LinkState::ReceiverNotFound) {
                return reading;
            }
            continue;
        }

        const auto result = exchange();
        if (result.error != ERROR_SUCCESS) {
            // Handle may be stale after re-enumeration; reopen on the next attempt.
            device_.reset();
            reading = makeReading(LinkState::Error,
                                  std::format(L"Receiver communication failed (error {})", result.error));
            continue;
        }

        reading = interpret(result.reply);
        if (reading.state != LinkState::Error) {
            return reading;
        }
        // A concurrent relay exchange (e.g. the vendor app) can leave a keyboard reply in the buffer.
        log::write(reading.detail);
    }
    return reading;
}

}  // namespace trayring
