#pragma once

// ROYUAN 2.4 GHz receiver status protocol (receiver opcode 0xF7).
// Pure data handling: no Windows or HID dependencies.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace trayring::ronin {

inline constexpr std::uint16_t kVendorId = 0x3151;
inline constexpr std::uint16_t kReceiverProductId = 0x4011;
inline constexpr std::uint16_t kVendorUsagePage = 0xFFFF;

// Feature report payload, excluding the report ID byte.
inline constexpr std::size_t kReportLength = 64;
// HidD_SetFeature/HidD_GetFeature buffer: report ID 0 followed by the payload.
inline constexpr std::size_t kFeatureBufferLength = kReportLength + 1;

inline constexpr std::uint8_t kOpcodeStatus = 0xF7;

using Report = std::array<std::uint8_t, kReportLength>;

enum class DeviceKind : std::uint8_t { Keyboard = 1, Mouse = 2, Both = 3 };

struct ReceiverStatus {
    bool relayReplyPending{};        // byte 0 == 1
    std::uint8_t keyboardBattery{};  // byte 1, percent
    std::uint8_t mouseBattery{};     // byte 2
    bool keyboardOnline{};           // byte 3 == 0
    bool mouseOnline{};              // byte 4 == 0
    bool readyToRelay{};             // byte 5 == 1
    DeviceKind kind{};               // byte 6

    [[nodiscard]] bool hasKeyboard() const noexcept {
        return kind == DeviceKind::Keyboard || kind == DeviceKind::Both;
    }
};

enum class ParseError { None, TooShort, NotAStatusReply };

struct ParseResult {
    std::optional<ReceiverStatus> status;
    ParseError error{ParseError::None};
};

// 0xFF - (sum of bytes 0..6), stored at byte 7 ("Bit7" checksum).
[[nodiscard]] std::uint8_t bit7Checksum(std::span<const std::uint8_t> report) noexcept;

[[nodiscard]] Report buildStatusRequest() noexcept;

[[nodiscard]] ParseResult parseStatusReply(std::span<const std::uint8_t> reply) noexcept;

[[nodiscard]] bool isValidBatteryPercent(std::uint8_t value) noexcept;

}  // namespace trayring::ronin
