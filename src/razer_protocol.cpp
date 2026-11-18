#include "razer_protocol.h"

namespace trayring::razer {

std::uint8_t crc(std::span<const std::uint8_t> report) noexcept {
    std::uint8_t value = 0;
    for (std::size_t i = 2; i < offset::kCrc && i < report.size(); ++i) {
        value ^= report[i];
    }
    return value;
}

Report buildBatteryRequest(std::uint8_t transactionId) noexcept {
    Report report{};
    report[offset::kStatus] = static_cast<std::uint8_t>(Status::New);
    report[offset::kTransactionId] = transactionId;
    report[offset::kDataSize] = 0x02;
    report[offset::kCommandClass] = kClassPower;
    report[offset::kCommandId] = kCommandBatteryLevel;
    report[offset::kCrc] = crc(report);
    return report;
}

BatteryReply parseBatteryReply(std::span<const std::uint8_t> reply, std::uint8_t transactionId) noexcept {
    BatteryReply result;
    if (reply.size() < kReportLength) {
        return result;
    }
    if (reply[offset::kTransactionId] != transactionId || reply[offset::kCommandClass] != kClassPower ||
        reply[offset::kCommandId] != kCommandBatteryLevel) {
        result.kind = ReplyKind::Unrelated;
        return result;
    }

    switch (static_cast<Status>(reply[offset::kStatus])) {
        case Status::Successful:
            if (reply[offset::kCrc] != crc(reply)) {
                return result;
            }
            result.kind = ReplyKind::Battery;
            result.rawLevel = reply[offset::kArguments + 1];
            result.percent = levelToPercent(result.rawLevel);
            return result;
        case Status::New:
        case Status::Busy:
            result.kind = ReplyKind::Busy;
            return result;
        case Status::Timeout:
            result.kind = ReplyKind::DeviceAsleep;
            return result;
        case Status::Failure:
        case Status::NotSupported:
            return result;
    }
    return result;
}

}  // namespace trayring::razer
