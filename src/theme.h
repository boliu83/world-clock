#pragma once
#include <windows.h>

namespace theme {

// Spacing (multiples of 4)
constexpr int kSpace1 = 4;
constexpr int kSpace2 = 8;
constexpr int kSpace3 = 12;
constexpr int kSpace4 = 16;
constexpr int kSpace6 = 24;
constexpr int kSpace8 = 32;

// Radii (DWM handles window rounding; radii used for backplates via GDI)
constexpr int kRadiusControl = 4;
constexpr int kRadiusOverlay = 8;

// Row metrics (base @ 96 DPI — scaled at use site)
constexpr int kRowHeight      = 56;
constexpr int kHeaderHeight   = 40;
constexpr int kFooterHeight   = 40;
constexpr int kPopupWidth     = 416;
constexpr int kPopupMaxRows   = 8;

// Type ramp (base pt-equivalent pixels @ 96 DPI)
constexpr int kTypeCaption = 12;
constexpr int kTypeBody    = 14;
constexpr int kTypeBodyStrong = 14;   // same size, weight 600
constexpr int kTypeSubtitle = 20;
constexpr int kTypeTitle    = 28;

// Color tokens (RGB). Alpha handled by alpha-blending where needed.
struct Palette {
    COLORREF bg;
    COLORREF layer;
    COLORREF controlBg;
    COLORREF controlHover;
    COLORREF controlPressed;
    COLORREF stroke;
    COLORREF strokeStrong;
    COLORREF text;
    COLORREF textSecondary;
    COLORREF textTertiary;
    COLORREF textDisabled;
    COLORREF accent;
};

bool IsDarkMode();
const Palette& CurrentPalette();
void InvalidatePaletteCache();

// Font cache keyed by (size, bold). Size is in DIPs; scaled to device px at creation time.
HFONT GetFont(int sizeDip, bool bold, UINT dpi);
HFONT GetIconFont(int sizeDip, UINT dpi);  // Segoe Fluent Icons

// Release all cached fonts/brushes (call at shutdown).
void ReleaseCaches();

// Scale a DIP value to physical pixels for a given DPI.
inline int Scale(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

// Simple rounded-rect fill using GDI (no anti-alias; acceptable at small radii).
void FillRoundRect(HDC hdc, RECT r, int radius, COLORREF fill);
void DrawRoundedBorder(HDC hdc, RECT r, int radius, COLORREF stroke);

// Draw text helper using UTF-16, respects color + font.
void DrawTextToken(HDC hdc, const wchar_t* text, RECT r, HFONT font, COLORREF color, UINT flags);

} // namespace theme
