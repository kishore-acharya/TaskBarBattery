#include "hid_device.h"

#include <cfgmgr32.h>
#include <winternl.h>  // NTSTATUS, required by the SDK's hidpi.h under MSVC
#include <hidsdi.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <iterator>

namespace trayring::hid {

UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
        reset();
        handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
    }
    return *this;
}

void UniqueHandle::reset() noexcept {
    if (valid()) {
        CloseHandle(handle_);
    }
    handle_ = INVALID_HANDLE_VALUE;
}

namespace {

std::vector<std::wstring> presentHidInterfacePaths() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);

    // The list can grow between the size query and the fetch, hence the retry loop.
    for (int attempt = 0; attempt < 5; ++attempt) {
        ULONG length = 0;
        if (CM_Get_Device_Interface_List_SizeW(&length, &hidGuid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
                CR_SUCCESS ||
            length == 0) {
            return {};
        }

        std::wstring buffer(length, L'\0');
        const auto result = CM_Get_Device_Interface_ListW(&hidGuid, nullptr, buffer.data(), length,
                                                          CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (result == CR_BUFFER_SMALL) {
            continue;
        }
        if (result != CR_SUCCESS) {
            return {};
        }

        std::vector<std::wstring> paths;
        for (const wchar_t* p = buffer.c_str(); *p != L'\0'; p += wcslen(p) + 1) {
            paths.emplace_back(p);
        }
        return paths;
    }
    return {};
}

bool containsVidPid(std::wstring path, std::uint16_t vendorId, std::uint16_t productId) {
    std::ranges::transform(path, path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    wchar_t token[32]{};
    swprintf(token, std::size(token), L"vid_%04x&pid_%04x", vendorId, productId);
    return path.find(token) != std::wstring::npos;
}

std::optional<InterfaceInfo> inspect(const std::wstring& path) {
    UniqueHandle handle{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                    nullptr)};
    if (!handle.valid()) {
        return std::nullopt;
    }

    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(handle.get(), &attributes)) {
        return std::nullopt;
    }

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(handle.get(), &preparsed)) {
        return std::nullopt;
    }
    HIDP_CAPS caps{};
    const auto status = HidP_GetCaps(preparsed, &caps);
    HidD_FreePreparsedData(preparsed);
    if (status != HIDP_STATUS_SUCCESS) {
        return std::nullopt;
    }

    return InterfaceInfo{
        .path = path,
        .vendorId = attributes.VendorID,
        .productId = attributes.ProductID,
        .versionNumber = attributes.VersionNumber,
        .usagePage = caps.UsagePage,
        .usage = caps.Usage,
        .inputReportLength = caps.InputReportByteLength,
        .outputReportLength = caps.OutputReportByteLength,
        .featureReportLength = caps.FeatureReportByteLength,
    };
}

}  // namespace

std::vector<InterfaceInfo> enumerate(std::uint16_t vendorId, std::uint16_t productId) {
    std::vector<InterfaceInfo> result;
    for (const auto& path : presentHidInterfacePaths()) {
        if (!containsVidPid(path, vendorId, productId)) {
            continue;
        }
        if (auto info = inspect(path); info && info->vendorId == vendorId && info->productId == productId) {
            result.push_back(std::move(*info));
        }
    }
    return result;
}

std::optional<Device> Device::open(const std::wstring& path, DWORD& error) {
    UniqueHandle handle{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr)};
    if (!handle.valid() && GetLastError() == ERROR_ACCESS_DENIED) {
        // Mouse/keyboard collections are held by the system; feature IOCTLs are FILE_ANY_ACCESS.
        handle = UniqueHandle{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                          0, nullptr)};
    }
    if (!handle.valid()) {
        error = GetLastError();
        return std::nullopt;
    }
    error = ERROR_SUCCESS;
    return Device{std::move(handle), path};
}

DWORD Device::setFeature(std::span<const std::uint8_t> buffer) const {
    // HidD_SetFeature takes a non-const PVOID but does not modify the buffer.
    if (!HidD_SetFeature(handle_.get(), const_cast<std::uint8_t*>(buffer.data()), static_cast<ULONG>(buffer.size()))) {
        return GetLastError();
    }
    return ERROR_SUCCESS;
}

DWORD Device::getFeature(std::span<std::uint8_t> buffer) const {
    if (!HidD_GetFeature(handle_.get(), buffer.data(), static_cast<ULONG>(buffer.size()))) {
        return GetLastError();
    }
    return ERROR_SUCCESS;
}

}  // namespace trayring::hid
