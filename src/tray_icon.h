#pragma once

#include "icon_renderer.h"

#include <windows.h>

namespace trayring::ui {

// Creates a `size`-pixel 32-bpp icon from renderIcon(). Caller owns it (DestroyIcon).
[[nodiscard]] HICON createTrayIcon(const IconSpec& spec, int size);

// Whether Windows draws the taskbar light (Settings > Personalization > Colors).
[[nodiscard]] bool taskbarUsesLightTheme();

}  // namespace trayring::ui
