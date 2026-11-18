#include "ronin_protocol.h"

namespace trayring::ronin {

std::uint8_t bit7Checksum(std::span<const std::uint8_t> report) noexcept {
    unsigned sum = 0;
    for (std::size_t i = 0; i < 7 && i < report.size(); ++i) {
        sum += report[i];
    }
    return static_cast<std::uint8_t>(0xFF - (sum & 0xFF));
}

Report buildStatusRequest() noexcept {
    Report report{};
    report[0] = kOpcodeStatus;
    report[7] = bit7Checksum(report);
    return report;
}

ParseResult parseStatusReply(std::span<const std::uint8_t> reply) noexcept {
    if (reply.size() < 7) {
        return {std::nullopt, ParseError::TooShort};
    }

    // Byte 6 is the only field a relayed keyboard reply cannot produce by accident.
    const auto kind = reply[6];
    if (kind < 1 || kind > 3) {
        return {std::nullopt, ParseError::NotAStatusReply};
    }

    return {ReceiverStatus{
                .relayReplyPending = reply[0] == 1,
                .keyboardBattery = reply[1],
                .mouseBattery = reply[2],
                .keyboardOnline = reply[3] == 0,
                .mouseOnline = reply[4] == 0,
                .readyToRelay = reply[5] == 1,
                .kind = static_cast<DeviceKind>(kind),
            },
            ParseError::None};
}

bool isValidBatteryPercent(std::uint8_t value) noexcept {
    return value <= 100;
}

}  // namespace trayring::ronin
