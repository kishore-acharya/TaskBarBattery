#include "tray_icon.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace trayring::ui {

HICON createTrayIcon(const IconSpec& spec, int size) {
    const auto pixels = renderIcon(spec, size);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (color == nullptr) {
        return nullptr;
    }
    std::ranges::copy(pixels, static_cast<std::uint32_t*>(bits));

    // With a 32-bpp colour bitmap the alpha channel defines transparency; the mask is unused.
    std::vector<std::uint8_t> maskBits(static_cast<std::size_t>((size + 15) / 16 * 2 * size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = mask;
    iconInfo.hbmColor = color;
    HICON icon = mask != nullptr ? CreateIconIndirect(&iconInfo) : nullptr;

    if (mask != nullptr) {
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

bool taskbarUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const auto status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value != 0;
}

}  // namespace trayring::ui
