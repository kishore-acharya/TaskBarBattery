#pragma once

// Per-user settings and the HKCU Run entry. Nothing here needs administrator rights.
namespace trayring::settings {

[[nodiscard]] bool isAutostartEnabled();
bool setAutostartEnabled(bool enabled);

// First launch enables autostart once; afterwards the user's choice is respected.
// Also repoints an existing Run entry at this executable if it has moved.
void initializeAutostart();

// HKCU\Software\TrayRing\DebugLog (DWORD) != 0
[[nodiscard]] bool isDebugLogEnabled();

}  // namespace trayring::settings
