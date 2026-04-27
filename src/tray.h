#pragma once
#include <windows.h>

namespace tray {

// Create the hidden owner window + add tray icon. Returns owner HWND.
HWND Create(HINSTANCE hInst);
void Destroy();

// Obtain the tray icon rect in screen coordinates (best-effort).
// Returns false if unavailable; caller should fall back to cursor pos.
bool GetIconRect(RECT& outScreenRect);

// Forwarded by owner wndproc when tray context menu items fire.
void HandleCommand(HWND hwnd, UINT cmdId);

} // namespace tray
