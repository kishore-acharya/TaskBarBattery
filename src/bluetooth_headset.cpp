#include "bluetooth_headset.h"

#include "logger.h"

#include <windows.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <format>
#include <optional>
#include <vector>

namespace trayring {

namespace {

// Bluetooth battery level reported over HFP (DEVPKEY_Bluetooth_Battery), on the Hands-Free AG node.
constexpr DEVPROPKEY kBatteryKey{{0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}}, 2};
// "Connected" on the BTHENUM\DEV_<address> node; true only while the link is up.
constexpr DEVPROPKEY kConnectedKey{{0x83DA6326, 0x97A6, 0x4088, {0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29}}, 15};
// DEVPKEY_Device_ContainerId: shared by all nodes of one physical device.
constexpr DEVPROPKEY kContainerIdKey{{0x8C7ED206, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}}, 2};

constexpr std::wstring_view kHandsFreeAgPrefix = L"bthenum\\{0000111e-0000-1000-8000-00805f9b34fb}";
constexpr std::wstring_view kDevicePrefix = L"bthenum\\dev_";

std::wstring toLower(std::wstring text) {
    std::ranges::transform(text, text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

std::vector<std::wstring> presentBluetoothNodes() {
    constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    for (;;) {
        ULONG length = 0;
        if (CM_Get_Device_ID_List_SizeW(&length, L"BTHENUM", flags) != CR_SUCCESS || length == 0) {
            return {};
        }
        std::vector<wchar_t> buffer(length);
        const auto result = CM_Get_Device_ID_ListW(L"BTHENUM", buffer.data(), length, flags);
        if (result == CR_BUFFER_SMALL) {
            continue;
        }
        if (result != CR_SUCCESS) {
            log::write(std::format(L"CM_Get_Device_ID_List failed ({})", result));
            return {};
        }
        std::vector<std::wstring> ids;
        for (const wchar_t* id = buffer.data(); *id != L'\0'; id += std::wcslen(id) + 1) {
            ids.emplace_back(id);
        }
        return ids;
    }
}

template <typename T>
std::optional<T> nodeProperty(const std::wstring& instanceId, const DEVPROPKEY& key, DEVPROPTYPE expectedType) {
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return std::nullopt;
    }
    T value{};
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    ULONG size = sizeof(value);
    if (CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<PBYTE>(&value), &size, 0) != CR_SUCCESS ||
        type != expectedType || size != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

BluetoothHeadset::BluetoothHeadset(std::wstring name, std::wstring hardwareId)
    : name_(std::move(name)), hardwareId_(toLower(std::move(hardwareId))) {}

bool BluetoothHeadset::ownsInterface(std::wstring_view lowerCasePath) const {
    return lowerCasePath.find(hardwareId_) != std::wstring_view::npos;
}

bluetooth::HandsFreeState BluetoothHeadset::query() const {
    bluetooth::HandsFreeState state;
    const auto nodes = presentBluetoothNodes();

    const auto ag = std::ranges::find_if(nodes, [this](const std::wstring& id) {
        const auto lower = toLower(id);
        return lower.starts_with(kHandsFreeAgPrefix) && lower.find(hardwareId_) != std::wstring::npos;
    });
    if (ag == nodes.end()) {
        return state;
    }
    state.paired = true;
    state.battery = nodeProperty<std::uint8_t>(*ag, kBatteryKey, DEVPROP_TYPE_BYTE);

    const auto container = nodeProperty<GUID>(*ag, kContainerIdKey, DEVPROP_TYPE_GUID);
    if (!container) {
        return state;
    }
    for (const auto& id : nodes) {
        if (toLower(id).starts_with(kDevicePrefix) &&
            nodeProperty<GUID>(id, kContainerIdKey, DEVPROP_TYPE_GUID) == container) {
            state.connected = nodeProperty<DEVPROP_BOOLEAN>(id, kConnectedKey, DEVPROP_TYPE_BOOLEAN) == DEVPROP_TRUE;
            break;
        }
    }
    return state;
}

}  // namespace trayring
