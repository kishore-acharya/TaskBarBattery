#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace trayring::hid {

struct InterfaceInfo {
    std::wstring path;
    std::uint16_t vendorId{};
    std::uint16_t productId{};
    std::uint16_t versionNumber{};
    std::uint16_t usagePage{};
    std::uint16_t usage{};
    std::uint16_t inputReportLength{};
    std::uint16_t outputReportLength{};
    std::uint16_t featureReportLength{};
};

class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept;
    ~UniqueHandle() { reset(); }

    void reset() noexcept;
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

// Lists every present HID collection with the given VID/PID, including capabilities.
// Collections are opened with zero access rights, so keyboard/mouse collections owned by
// the system are inspected without reading or disturbing their input.
[[nodiscard]] std::vector<InterfaceInfo> enumerate(std::uint16_t vendorId, std::uint16_t productId);

// A shared (non-exclusive) handle to a HID collection for feature reports. Opened read/write when
// possible, otherwise with zero access (enough for HidD_Get/SetFeature on system-owned collections).
class Device {
public:
    [[nodiscard]] static std::optional<Device> open(const std::wstring& path, DWORD& error);

    // Buffers include the leading report ID byte. Return ERROR_SUCCESS or a Win32 error.
    [[nodiscard]] DWORD setFeature(std::span<const std::uint8_t> buffer) const;
    [[nodiscard]] DWORD getFeature(std::span<std::uint8_t> buffer) const;

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

private:
    Device(UniqueHandle handle, std::wstring path) noexcept : handle_(std::move(handle)), path_(std::move(path)) {}

    UniqueHandle handle_;
    std::wstring path_;
};

}  // namespace trayring::hid
