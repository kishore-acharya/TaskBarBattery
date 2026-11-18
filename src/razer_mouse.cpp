#include "razer_mouse.h"

#include "logger.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>

namespace trayring {

namespace {

constexpr int kMaxAttempts = 5;
// Each GET takes ~15 ms on this dongle; the first one after the SET usually returns Busy.
constexpr int kMaxPolls = 6;
constexpr DWORD kRetryDelayMs = 50;

RazerReading makeReading(LinkState state, std::wstring detail) {
    RazerReading reading;
    reading.state = state;
    reading.detail = std::move(detail);
    return reading;
}

bool isFirstInterface(const hid::InterfaceInfo& info) {
    std::wstring lower = info.path;
    std::ranges::transform(lower, lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return lower.find(L"&mi_00") != std::wstring::npos;
}

}  // namespace

std::vector<hid::InterfaceInfo> razerControlInterfaces(std::uint16_t productId) {
    auto interfaces = hid::enumerate(razer::kVendorId, productId);
    std::erase_if(interfaces,
                  [](const hid::InterfaceInfo& i) { return i.featureReportLength != razer::kFeatureBufferLength; });
    std::ranges::stable_partition(interfaces, isFirstInterface);
    return interfaces;
}

bool RazerDeathAdderV2X::ownsInterface(std::wstring_view lowerCasePath) const {
    return lowerCasePath.find(L"vid_1532&pid_009c") != std::wstring_view::npos;
}

bool RazerDeathAdderV2X::ensureOpen(RazerReading& reading) {
    if (device_) {
        return true;
    }

    const auto interfaces = hid::enumerate(razer::kVendorId, razer::kDeathAdderV2XHyperSpeed);
    if (interfaces.empty()) {
        reading = makeReading(LinkState::ReceiverNotFound, L"HyperSpeed dongle not found");
        return false;
    }

    DWORD error = ERROR_NOT_FOUND;
    for (const auto& candidate : razerControlInterfaces(razer::kDeathAdderV2XHyperSpeed)) {
        if ((device_ = hid::Device::open(candidate.path, error))) {
            log::write(std::format(L"Opened {}", candidate.path));
            return true;
        }
        log::write(std::format(L"Cannot open {} (error {})", candidate.path, error));
    }
    reading = makeReading(LinkState::Error, std::format(L"Razer control interface unavailable (error {})", error));
    return false;
}

// Synapse shares the dongle and overwrites the single reply buffer with its own traffic within
// ~20 ms, so the reply is polled immediately instead of after a fixed delay.
RazerDeathAdderV2X::Exchange RazerDeathAdderV2X::exchange() const {
    Exchange result;

    std::array<std::uint8_t, razer::kFeatureBufferLength> request{};
    const auto payload = razer::buildBatteryRequest();
    std::ranges::copy(payload, request.begin() + 1);
    if (result.error = device_->setFeature(request); result.error != ERROR_SUCCESS) {
        log::write(std::format(L"Razer HidD_SetFeature failed (error {})", result.error));
        return result;
    }

    for (int poll = 0; poll < kMaxPolls; ++poll) {
        std::array<std::uint8_t, razer::kFeatureBufferLength> reply{};
        if (result.error = device_->getFeature(reply); result.error != ERROR_SUCCESS) {
            log::write(std::format(L"Razer HidD_GetFeature failed (error {})", result.error));
            return result;
        }
        std::ranges::copy(reply.begin() + 1, reply.end(), result.reply.begin());
        result.parsed = razer::parseBatteryReply(result.reply);
        if (result.parsed.kind != razer::ReplyKind::Busy) {
            break;
        }
    }
    return result;
}

RazerReading RazerDeathAdderV2X::readDetailed() {
    RazerReading reading;
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
            device_.reset();
            reading = makeReading(LinkState::Error,
                                  std::format(L"Dongle communication failed (error {})", result.error));
            continue;
        }

        const auto& parsed = result.parsed;
        reading = RazerReading{};
        reading.rawReply = result.reply;
        reading.interfacePath = device_->path();
        switch (parsed.kind) {
            case razer::ReplyKind::Battery:
                reading.state = LinkState::Online;
                reading.percent = *parsed.percent;
                reading.detail = std::format(L"Battery {}%", reading.percent);
                return reading;
            case razer::ReplyKind::DeviceAsleep:
                reading.state = LinkState::DeviceOffline;
                reading.detail = L"Mouse asleep or switched off";
                return reading;
            case razer::ReplyKind::Busy:
                // Still busy after every poll: the dongle is waiting for a mouse that does not answer.
                reading.state = LinkState::DeviceOffline;
                reading.detail = L"No answer from mouse";
                break;
            case razer::ReplyKind::Unrelated:
                // Synapse shares the dongle; its reply may have been read instead of ours.
                reading.state = LinkState::Error;
                reading.detail = L"Reply to another request";
                break;
            case razer::ReplyKind::Failed:
                reading.state = LinkState::Error;
                reading.detail = std::format(L"Battery query failed (status {:#04x})", result.reply[0]);
                break;
        }
        log::write(reading.detail);
    }
    return reading;
}

}  // namespace trayring
