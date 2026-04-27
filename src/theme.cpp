#include "theme.h"
#include <unordered_map>
#include <string>

namespace theme {

namespace {

Palette gLight{
    /*bg*/            RGB(0xF3,0xF3,0xF3),
    /*layer*/         RGB(0xFF,0xFF,0xFF),
    /*controlBg*/     RGB(0xF7,0xF7,0xF7),
    /*controlHover*/  RGB(0xEC,0xEC,0xEC),
    /*controlPressed*/RGB(0xE5,0xE5,0xE5),
    /*stroke*/        RGB(0xE1,0xE1,0xE1),
    /*strokeStrong*/  RGB(0x8C,0x8C,0x8C),
    /*text*/          RGB(0x1A,0x1A,0x1A),
    /*textSecondary*/ RGB(0x60,0x60,0x60),
    /*textTertiary*/  RGB(0x8C,0x8C,0x8C),
    /*textDisabled*/  RGB(0xA6,0xA6,0xA6),
    /*accent*/        RGB(0x00,0x5F,0xB8),
};

Palette gDark{
    /*bg*/            RGB(0x20,0x20,0x20),
    /*layer*/         RGB(0x2B,0x2B,0x2B),
    /*controlBg*/     RGB(0x33,0x33,0x33),
    /*controlHover*/  RGB(0x3D,0x3D,0x3D),
    /*controlPressed*/RGB(0x2D,0x2D,0x2D),
    /*stroke*/        RGB(0x3D,0x3D,0x3D),
    /*strokeStrong*/  RGB(0x8A,0x8A,0x8A),
    /*text*/          RGB(0xFF,0xFF,0xFF),
    /*textSecondary*/ RGB(0xC8,0xC8,0xC8),
    /*textTertiary*/  RGB(0x9A,0x9A,0x9A),
    /*textDisabled*/  RGB(0x6E,0x6E,0x6E),
    /*accent*/        RGB(0x60,0xCD,0xFF),
};

struct FontKey {
    int size; bool bold; UINT dpi; bool icon;
    bool operator==(const FontKey& o) const {
        return size==o.size && bold==o.bold && dpi==o.dpi && icon==o.icon;
    }
};
struct FontKeyHash {
    size_t operator()(const FontKey& k) const noexcept {
        return (size_t)k.size * 131 + (size_t)k.dpi * 17
             + (k.bold?1:0) + (k.icon?2:0);
    }
};

std::unordered_map<FontKey, HFONT, FontKeyHash> gFonts;
int gCachedIsDark = -1;

HFONT CreateOneFont(const FontKey& k) {
    LOGFONTW lf{};
    lf.lfHeight = -Scale(k.size, k.dpi);  // size in DIPs → px
    lf.lfWeight = k.bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    if (k.icon) {
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
        HFONT f = CreateFontIndirectW(&lf);
        if (f) return f;
        wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
        return CreateFontIndirectW(&lf);
    }
    wcscpy_s(lf.lfFaceName, L"Segoe UI Variable");
    HFONT f = CreateFontIndirectW(&lf);
    if (f) return f;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

} // namespace

bool IsDarkMode() {
    if (gCachedIsDark >= 0) return gCachedIsDark != 0;
    DWORD value = 1, size = sizeof(value);
    LSTATUS s = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    bool dark = (s == ERROR_SUCCESS) ? (value == 0) : false;
    gCachedIsDark = dark ? 1 : 0;
    return dark;
}

const Palette& CurrentPalette() {
    return IsDarkMode() ? gDark : gLight;
}

void InvalidatePaletteCache() {
    gCachedIsDark = -1;
}

HFONT GetFont(int sizeDip, bool bold, UINT dpi) {
    FontKey k{sizeDip, bold, dpi, false};
    auto it = gFonts.find(k);
    if (it != gFonts.end()) return it->second;
    HFONT f = CreateOneFont(k);
    gFonts.emplace(k, f);
    return f;
}

HFONT GetIconFont(int sizeDip, UINT dpi) {
    FontKey k{sizeDip, false, dpi, true};
    auto it = gFonts.find(k);
    if (it != gFonts.end()) return it->second;
    HFONT f = CreateOneFont(k);
    gFonts.emplace(k, f);
    return f;
}

void ReleaseCaches() {
    for (auto& kv : gFonts) DeleteObject(kv.second);
    gFonts.clear();
}

void FillRoundRect(HDC hdc, RECT r, int radius, COLORREF fill) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(hdc, b);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius*2, radius*2);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(b);
    DeleteObject(pen);
}

void DrawRoundedBorder(HDC hdc, RECT r, int radius, COLORREF stroke) {
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HGDIOBJ op = SelectObject(hdc, pen);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius*2, radius*2);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pen);
}

void DrawTextToken(HDC hdc, const wchar_t* text, RECT r, HFONT font, COLORREF color, UINT flags) {
    HGDIOBJ of = SelectObject(hdc, font);
    int oldMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &r, flags);
    SetTextColor(hdc, oldColor);
    SetBkMode(hdc, oldMode);
    SelectObject(hdc, of);
}

} // namespace theme
