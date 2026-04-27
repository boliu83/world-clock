#pragma once
#include <windows.h>

namespace popup {

void Initialize(HINSTANCE hInst);
void Shutdown();

void Show();
void Hide();
void Toggle();

// Recompute layout & repaint (settings changed).
void Refresh();

// Apply always-on-top setting to the popup HWND.
void ApplyAlwaysOnTop();

// Clear time override and refresh.
void ResetTime();

} // namespace popup
