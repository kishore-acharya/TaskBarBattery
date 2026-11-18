#pragma once

// Razer 90-byte control report, as documented by OpenRazer (driver/razercommon.h).
// Pure data handling: no Windows or HID dependencies.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace trayring::razer {

inline constexpr std::uint16_t kVendorId = 0x1532;
inline constexpr std::uint16_t kDeathAdderV2XHyperSpeed = 0x009C;

inline constexpr std::size_t kReportLength = 90;
// HidD_SetFeature/HidD_GetFeature buffer: report ID 0 followed by the report.
inline constexpr std::size_t kFeatureBufferLength = kReportLength + 1;

// Transaction ID OpenRazer uses for the DeathAdder V2 X HyperSpeed battery query.
inline constexpr std::uint8_t kTransactionId = 0x1F;

inline constexpr std::uint8_t kClassPower = 0x07;
inline constexpr std::uint8_t kCommandBatteryLevel = 0x80;

using Report = std::array<std::uint8_t, kReportLength>;

enum class Status : std::uint8_t {
    New = 0x00,
    Busy = 0x01,
    Successful = 0x02,
    Failure = 0x03,
    Timeout = 0x04,  // receiver got no answer from the mouse (asleep / off)
    NotSupported = 0x05,
};

namespace offset {
inline constexpr std::size_t kStatus = 0;
inline constexpr std::size_t kTransactionId = 1;
inline constexpr std::size_t kDataSize = 5;
inline constexpr std::size_t kCommandClass = 6;
inline constexpr std::size_t kCommandId = 7;
inline constexpr std::size_t kArguments = 8;
inline constexpr std::size_t kCrc = 88;
}  // namespace offset

// XOR of bytes 2..87.
[[nodiscard]] std::uint8_t crc(std::span<const std::uint8_t> report) noexcept;

[[nodiscard]] Report buildBatteryRequest(std::uint8_t transactionId = kTransactionId) noexcept;

enum class ReplyKind {
    Battery,       // successful reply to the battery query
    Busy,          // retry later
    DeviceAsleep,  // timeout: the receiver could not reach the mouse
    Unrelated,     // a reply to another command / transaction (e.g. Synapse traffic)
    Failed,        // failure, not supported or malformed
};

struct BatteryReply {
    ReplyKind kind{ReplyKind::Failed};
    std::optional<int> percent;  // only for ReplyKind::Battery
    std::uint8_t rawLevel{};     // 0..255
};

[[nodiscard]] BatteryReply parseBatteryReply(std::span<const std::uint8_t> reply,
                                             std::uint8_t transactionId = kTransactionId) noexcept;

// Scales the device's 0..255 level to a percentage, truncating like Razer Synapse (71 -> 27%).
[[nodiscard]] constexpr int levelToPercent(std::uint8_t level) noexcept { return level * 100 / 255; }

}  // namespace trayring::razer
