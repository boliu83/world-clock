#include "popup.h"
#include "resource.h"
#include "theme.h"
#include "settings.h"
#include "tz.h"
#include "clock_engine.h"
#include "tray.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace popup {

namespace {

// ---- DWM attribute ids (not in older SDKs) ----
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

constexpr wchar_t kClass[]       = L"WorldClockPopup";
constexpr wchar_t kPickerClass[] = L"WorldClockPicker";
constexpr UINT_PTR kTimerId      = 1;
constexpr int kTickIntervalMs    = 1000;

HINSTANCE gInst     = nullptr;
HWND      gHwnd     = nullptr;
HWND      gList     = nullptr;
HWND      gBtnAdd   = nullptr;
HWND      gBtnFormat= nullptr;
HWND      gBtnReset = nullptr;
HWND      gEditInline = nullptr;
HICON     gTitleIcon = nullptr;
int       gEditRow  = -1;
UINT      gDpi      = 96;
DWORD     gShownAtTick = 0;   // GetTickCount() when last shown
constexpr DWORD kDismissGraceMs = 300;

// Add-timezone picker state
HWND gPicker       = nullptr;
HWND gPickerSearch = nullptr;
HWND gPickerList   = nullptr;
std::vector<int> gPickerIndices;   // filtered indices into tz::AllZones()

// Drag-to-reorder state
int  gDragFrom = -1;
int  gDragTo   = -1;
bool gDragging = false;
POINT gDragStart{};

// ---- Helpers ----

int RowHeightPx()    { return theme::Scale(theme::kRowHeight, gDpi); }
int HeaderHeightPx() { return theme::Scale(theme::kHeaderHeight, gDpi); }
int FooterHeightPx() { return theme::Scale(theme::kFooterHeight, gDpi); }
int PopupWidthPx()   { return theme::Scale(theme::kPopupWidth, gDpi); }

std::wstring FormatTime(int h, int m, int s, bool use24h, bool showSeconds) {
    wchar_t buf[32];
    if (use24h) {
        if (showSeconds) swprintf_s(buf, L"%02d:%02d:%02d", h, m, s);
        else             swprintf_s(buf, L"%02d:%02d", h, m);
    } else {
        const wchar_t* ap = (h < 12) ? L"AM" : L"PM";
        int h12 = h % 12; if (h12 == 0) h12 = 12;
        if (showSeconds) swprintf_s(buf, L"%d:%02d:%02d %s", h12, m, s, ap);
        else             swprintf_s(buf, L"%d:%02d %s", h12, m, ap);
    }
    return buf;
}

std::wstring FormatDateSubline(const tz::Fields& f) {
    static const wchar_t* kMon[] = {L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",
                                    L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};
    wchar_t buf[64];
    int sign = f.gmtOffsetMinutes >= 0 ? 1 : -1;
    int tot  = f.gmtOffsetMinutes * sign;
    swprintf_s(buf, L"%s %d · GMT%c%d:%02d%s",
               kMon[(f.month-1) & 11], f.day,
               sign >= 0 ? L'+' : L'-', tot / 60, tot % 60,
               f.isDst ? L" (DST)" : L"");
    return buf;
}

void ApplyDwmChrome(HWND hwnd) {
    BOOL dark = theme::IsDarkMode() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

void PositionPopup(HWND hwnd) {
    auto& s = settings::Get();
    int w = PopupWidthPx();
    int rowCount = s.popupHeightRows;
    if (rowCount == 0) {
        rowCount = std::min<int>(static_cast<int>(s.zones.size()), theme::kPopupMaxRows);
        if (rowCount < 1) rowCount = 1;
    } else {
        rowCount = std::clamp(rowCount, 0, 20);
    }
    int h = HeaderHeightPx() + rowCount * RowHeightPx() + FooterHeightPx();

    POINT anchor{};
    RECT tr{};
    if (tray::GetIconRect(tr)) {
        anchor.x = (tr.left + tr.right) / 2;
        anchor.y = tr.top;
    } else {
        GetCursorPos(&anchor);
    }

    HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    int maxH = mi.rcWork.bottom - mi.rcWork.top - 16;
    if (h > maxH) h = maxH;

    int x = anchor.x - w / 2;
    int y = anchor.y - h - theme::Scale(theme::kSpace2, gDpi);
    if (x < mi.rcWork.left + 8)       x = mi.rcWork.left + 8;
    if (x + w > mi.rcWork.right - 8)  x = mi.rcWork.right - 8 - w;
    if (y < mi.rcWork.top + 8)        y = mi.rcWork.top + 8;
    if (y + h > mi.rcWork.bottom - 8) y = mi.rcWork.bottom - 8 - h;

    SetWindowPos(hwnd,
        settings::Get().alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
        x, y, w, h, SWP_NOACTIVATE);
}

void LayoutChildren(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int headerH = HeaderHeightPx();
    int footerH = FooterHeightPx();
    int pad     = theme::Scale(theme::kSpace2, gDpi);
    int btn     = theme::Scale(28, gDpi);
    int gap     = theme::Scale(theme::kSpace1, gDpi);

    // Header buttons (right-aligned): add, format
    int bx = rc.right - pad - btn;
    SetWindowPos(gBtnAdd,    nullptr, bx, (headerH - btn)/2, btn, btn, SWP_NOZORDER);
    bx -= btn + gap;
    SetWindowPos(gBtnFormat, nullptr, bx, (headerH - btn)/2, btn, btn, SWP_NOZORDER);

    // List
    SetWindowPos(gList, nullptr,
        0, headerH,
        rc.right, rc.bottom - headerH - footerH,
        SWP_NOZORDER);

    // Footer reset button (right-aligned)
    int fbw = theme::Scale(80, gDpi);
    int fbh = theme::Scale(28, gDpi);
    SetWindowPos(gBtnReset, nullptr,
        rc.right - pad - fbw,
        rc.bottom - footerH + (footerH - fbh)/2,
        fbw, fbh, SWP_NOZORDER);
    ShowWindow(gBtnReset, clock_engine::IsOverridden() ? SW_SHOW : SW_HIDE);
}

void PaintHeaderFooter(HDC hdc, const RECT& client) {
    const auto& p = theme::CurrentPalette();

    // Header
    RECT hr = client; hr.bottom = hr.top + HeaderHeightPx();
    {
        HBRUSH b = CreateSolidBrush(p.bg);
        FillRect(hdc, &hr, b);
        DeleteObject(b);
    }
    // Title
    RECT tr = hr;
    tr.left += theme::Scale(theme::kSpace4, gDpi);
    if (gTitleIcon) {
        int iconSize = theme::Scale(20, gDpi);
        int iconY = tr.top + (HeaderHeightPx() - iconSize) / 2;
        DrawIconEx(hdc, tr.left, iconY, gTitleIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        tr.left += iconSize + theme::Scale(theme::kSpace2, gDpi);
    }
    theme::DrawTextToken(hdc, L"World clock", tr,
        theme::GetFont(theme::kTypeSubtitle, true, gDpi),
        p.text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    // Footer
    RECT fr = client; fr.top = fr.bottom - FooterHeightPx();
    {
        HBRUSH b = CreateSolidBrush(p.bg);
        FillRect(hdc, &fr, b);
        DeleteObject(b);
    }
    // Top separator lines
    HPEN pen = CreatePen(PS_SOLID, 1, p.stroke);
    HGDIOBJ op = SelectObject(hdc, pen);
    MoveToEx(hdc, fr.left, fr.top, nullptr);
    LineTo(hdc, fr.right, fr.top);
    MoveToEx(hdc, hr.left, hr.bottom - 1, nullptr);
    LineTo(hdc, hr.right, hr.bottom - 1);
    SelectObject(hdc, op);
    DeleteObject(pen);

    // Footer left text
    if (clock_engine::IsOverridden()) {
        RECT lr = fr;
        lr.left += theme::Scale(theme::kSpace4, gDpi);
        theme::DrawTextToken(hdc, L"Previewing a chosen time", lr,
            theme::GetFont(theme::kTypeCaption, false, gDpi),
            p.textSecondary, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
}

void DrawListItem(DRAWITEMSTRUCT* dis) {
    if (dis->itemID == (UINT)-1) return;
    auto& zones = settings::Get().zones;
    if (dis->itemID >= zones.size()) return;

    const auto& p = theme::CurrentPalette();
    RECT r = dis->rcItem;

    // Backplate
    HBRUSH bgBrush = CreateSolidBrush(p.layer);
    FillRect(dis->hDC, &r, bgBrush);
    DeleteObject(bgBrush);

    bool hovered  = (dis->itemState & ODS_HOTLIGHT) || (dis->itemState & ODS_SELECTED);
    if (hovered) {
        RECT pad = r;
        int m = theme::Scale(theme::kSpace1, gDpi);
        pad.left += m; pad.right -= m; pad.top += m/2; pad.bottom -= m/2;
        theme::FillRoundRect(dis->hDC, pad, theme::kRadiusControl, p.controlHover);
    }

    // Fields
    tz::Fields f{};
    if (!tz::FormatMillis(zones[dis->itemID], clock_engine::NowUtcMs(), f)) return;

    auto& st = settings::Get();
    std::wstring city = tz::FriendlyLocation(zones[dis->itemID]);
    std::wstring sub  = FormatDateSubline(f);
    std::wstring time = FormatTime(f.hour, f.minute, f.second, st.use24h, st.showSeconds);

    int padL = theme::Scale(theme::kSpace4, gDpi);
    int padR = theme::Scale(theme::kSpace4, gDpi);
    int closeW = theme::Scale(24, gDpi);

    // Right-aligned time
    RECT timeR = r;
    timeR.right -= padR + (hovered ? closeW + theme::Scale(theme::kSpace1, gDpi) : 0);
    timeR.left  = timeR.right - theme::Scale(140, gDpi);
    if (!(gEditInline && gEditRow == (int)dis->itemID)) {
        theme::DrawTextToken(dis->hDC, time.c_str(), timeR,
            theme::GetFont(theme::kTypeTitle, true, gDpi),
            p.text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }

    // Left: city (body strong), subline (caption secondary)
    RECT cityR = r;
    cityR.left += padL;
    cityR.right = timeR.left - theme::Scale(theme::kSpace2, gDpi);
    cityR.top += theme::Scale(theme::kSpace2, gDpi);
    cityR.bottom = cityR.top + theme::Scale(20, gDpi);
    theme::DrawTextToken(dis->hDC, city.c_str(), cityR,
        theme::GetFont(theme::kTypeBodyStrong, true, gDpi),
        p.text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    RECT subR = cityR;
    subR.top = cityR.bottom;
    subR.bottom = subR.top + theme::Scale(16, gDpi);
    theme::DrawTextToken(dis->hDC, sub.c_str(), subR,
        theme::GetFont(theme::kTypeCaption, false, gDpi),
        p.textSecondary, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    // Close glyph on hover
    if (hovered) {
        RECT xr = r;
        xr.right -= padR;
        xr.left = xr.right - closeW;
        theme::DrawTextToken(dis->hDC, L"\uE711", xr,
            theme::GetIconFont(14, gDpi),
            p.textSecondary, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }

    // Drag insertion line
    if (gDragging && gDragTo == (int)dis->itemID && gDragTo != gDragFrom) {
        HPEN pen = CreatePen(PS_SOLID, 2, p.accent);
        HGDIOBJ op = SelectObject(dis->hDC, pen);
        int y = (gDragTo > gDragFrom) ? r.bottom - 1 : r.top;
        MoveToEx(dis->hDC, r.left + padL, y, nullptr);
        LineTo(dis->hDC, r.right - padR, y);
        SelectObject(dis->hDC, op);
        DeleteObject(pen);
    }
}

// Returns 1 if the point is inside the close glyph for the hovered row.
bool IsPointOnCloseGlyph(int x, int itemIndex) {
    RECT r; SendMessageW(gList, LB_GETITEMRECT, itemIndex, (LPARAM)&r);
    int padR = theme::Scale(theme::kSpace4, gDpi);
    int closeW = theme::Scale(24, gDpi);
    return x >= (r.right - padR - closeW) && x <= (r.right - padR);
}

void CommitInlineEdit() {
    if (!gEditInline || gEditRow < 0) return;
    wchar_t buf[32]; GetWindowTextW(gEditInline, buf, 32);
    int h = 0, m = 0, s = 0;
    int n = swscanf_s(buf, L"%d:%d:%d", &h, &m, &s);
    if (n < 2) { DestroyWindow(gEditInline); gEditInline = nullptr; gEditRow = -1; return; }
    // Handle AM/PM if user typed it.
    std::wstring ws = buf;
    std::transform(ws.begin(), ws.end(), ws.begin(), ::towlower);
    if (ws.find(L"pm") != std::wstring::npos && h < 12) h += 12;
    if (ws.find(L"am") != std::wstring::npos && h == 12) h = 0;

    auto& zones = settings::Get().zones;
    if (gEditRow < (int)zones.size()) {
        tz::Fields f{};
        if (tz::FormatMillis(zones[gEditRow], clock_engine::NowUtcMs(), f)) {
            clock_engine::SetFromLocal(zones[gEditRow].c_str(),
                f.year, f.month, f.day,
                std::clamp(h, 0, 23),
                std::clamp(m, 0, 59),
                std::clamp(s, 0, 59));
        }
    }
    DestroyWindow(gEditInline); gEditInline = nullptr; gEditRow = -1;
    Refresh();
}

void CancelInlineEdit() {
    if (gEditInline) { DestroyWindow(gEditInline); gEditInline = nullptr; }
    gEditRow = -1;
    InvalidateRect(gList, nullptr, TRUE);
}

LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { CommitInlineEdit(); return 0; }
        if (wp == VK_ESCAPE) { CancelInlineEdit(); return 0; }
    } else if (msg == WM_KILLFOCUS) {
        // Focus left the edit for real. Commit and move on.
        CommitInlineEdit();
    } else if (msg == WM_CHAR) {
        wchar_t c = (wchar_t)wp;
        if (!(iswdigit(c) || c == L':' || c == L' ' || c == VK_BACK
              || c == L'a' || c == L'A' || c == L'p' || c == L'P'
              || c == L'm' || c == L'M')) return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void StartInlineEdit(int row) {
    auto& zones = settings::Get().zones;
    if (row < 0 || row >= (int)zones.size()) return;
    RECT ir; SendMessageW(gList, LB_GETITEMRECT, row, (LPARAM)&ir);
    MapWindowPoints(gList, gHwnd, (LPPOINT)&ir, 2);

    int padR = theme::Scale(theme::kSpace4, gDpi);
    RECT er = ir;
    er.right -= padR;
    er.left = er.right - theme::Scale(140, gDpi);
    er.top    += theme::Scale(theme::kSpace2, gDpi);
    er.bottom -= theme::Scale(theme::kSpace2, gDpi);

    tz::Fields f{};
    if (!tz::FormatMillis(zones[row], clock_engine::NowUtcMs(), f)) return;
    auto& st = settings::Get();
    std::wstring init = FormatTime(f.hour, f.minute, f.second, st.use24h, false);

    gEditRow = row;
    gEditInline = CreateWindowExW(0, L"EDIT", init.c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
        er.left, er.top, er.right - er.left, er.bottom - er.top,
        gHwnd, (HMENU)(INT_PTR)IDC_EDIT_INLINE, gInst, nullptr);
    SendMessageW(gEditInline, WM_SETFONT,
        (WPARAM)theme::GetFont(theme::kTypeTitle, true, gDpi), TRUE);
    SendMessageW(gEditInline, EM_SETSEL, 0, -1);
    SetFocus(gEditInline);
    SetWindowSubclass(gEditInline, EditSubclassProc, 1, 0);
}

// ----- Add-timezone picker -----

// Common TZ abbreviations mapped to IANA substrings. Keys must be lowercase.
// Multiple IANA ids per alias are matched with OR semantics.
struct AliasEntry { const wchar_t* alias; const char* ianaSubstr; };
static const AliasEntry kAliases[] = {
    {L"ist",  "Asia/Kolkata"},       // India Standard Time (most common)
    {L"ist",  "Europe/Dublin"},      // Irish Standard Time
    {L"ist",  "Asia/Jerusalem"},     // Israel Standard Time
    {L"ist",  "Asia/Tehran"},        // Iran Standard Time
    {L"pst",  "America/Los_Angeles"},
    {L"pdt",  "America/Los_Angeles"},
    {L"mst",  "America/Denver"},
    {L"mdt",  "America/Denver"},
    {L"cst",  "America/Chicago"},
    {L"cdt",  "America/Chicago"},
    {L"est",  "America/New_York"},
    {L"edt",  "America/New_York"},
    {L"akst", "America/Anchorage"},
    {L"hst",  "Pacific/Honolulu"},
    {L"gmt",  "Europe/London"},
    {L"bst",  "Europe/London"},      // British Summer Time
    {L"cet",  "Europe/Paris"},
    {L"cest", "Europe/Paris"},
    {L"eet",  "Europe/Athens"},
    {L"msk",  "Europe/Moscow"},
    {L"jst",  "Asia/Tokyo"},
    {L"kst",  "Asia/Seoul"},
    {L"sgt",  "Asia/Singapore"},
    {L"hkt",  "Asia/Hong_Kong"},
    {L"cst_china", "Asia/Shanghai"}, // disambiguate from US CST
    {L"aest", "Australia/Sydney"},
    {L"aedt", "Australia/Sydney"},
    {L"awst", "Australia/Perth"},
    {L"nzst", "Pacific/Auckland"},
    {L"nzdt", "Pacific/Auckland"},
    {L"utc",  "UTC"},
};

static bool AliasMatches(const std::wstring& filterLo, const std::string& iana) {
    for (const auto& a : kAliases) {
        if (filterLo == a.alias && iana.find(a.ianaSubstr) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void RefreshPickerList(const std::wstring& filter) {
    SendMessageW(gPickerList, LB_RESETCONTENT, 0, 0);
    gPickerIndices.clear();
    std::wstring f = filter;
    std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    const auto& all = tz::AllZones();
    for (int i = 0; i < (int)all.size(); ++i) {
        std::wstring ws(all[i].begin(), all[i].end());
        std::wstring lo = ws;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::towlower);
        bool match = f.empty()
            || lo.find(f) != std::wstring::npos
            || AliasMatches(f, all[i]);
        if (match) {
            SendMessageW(gPickerList, LB_ADDSTRING, 0, (LPARAM)ws.c_str());
            gPickerIndices.push_back(i);
            if (gPickerIndices.size() >= 500) break;
        }
    }
}

LRESULT CALLBACK PickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            gPickerSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)IDC_PICKER_SEARCH, gInst, nullptr);
            gPickerList = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_PICKER_LIST, gInst, nullptr);
            HFONT font = theme::GetFont(theme::kTypeBody, false, gDpi);
            SendMessageW(gPickerSearch, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(gPickerList,   WM_SETFONT, (WPARAM)font, TRUE);
            RefreshPickerList(L"");
            SetFocus(gPickerSearch);
            return 0;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hwnd, &rc);
            int pad = theme::Scale(theme::kSpace2, gDpi);
            int sh  = theme::Scale(28, gDpi);
            SetWindowPos(gPickerSearch, nullptr, pad, pad, rc.right - pad*2, sh, SWP_NOZORDER);
            SetWindowPos(gPickerList, nullptr,
                pad, pad*2 + sh, rc.right - pad*2, rc.bottom - pad*3 - sh, SWP_NOZORDER);
            return 0;
        }
        case WM_COMMAND: {
            UINT id = LOWORD(wp);
            UINT code = HIWORD(wp);
            if (id == IDC_PICKER_SEARCH && code == EN_CHANGE) {
                wchar_t buf[128]; GetWindowTextW(gPickerSearch, buf, 128);
                RefreshPickerList(buf);
                return 0;
            }
            if (id == IDC_PICKER_LIST && code == LBN_DBLCLK) {
                int sel = (int)SendMessageW(gPickerList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)gPickerIndices.size()) {
                    const auto& all = tz::AllZones();
                    settings::Get().zones.push_back(all[gPickerIndices[sel]]);
                    settings::Save();
                    DestroyWindow(hwnd);
                    Refresh();
                }
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            gPicker = nullptr;
            gPickerSearch = nullptr;
            gPickerList = nullptr;
            gPickerIndices.clear();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowPicker() {
    if (gPicker) { SetForegroundWindow(gPicker); return; }
    int w = theme::Scale(280, gDpi);
    int h = theme::Scale(360, gDpi);

    RECT pr; GetWindowRect(gHwnd, &pr);
    int x = pr.right - w;
    int y = pr.top - h - theme::Scale(theme::kSpace1, gDpi);

    HMONITOR mon = MonitorFromWindow(gHwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    if (y < mi.rcWork.top + 8) y = pr.bottom + theme::Scale(theme::kSpace1, gDpi);

    gPicker = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPickerClass,
        L"Add timezone",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, gHwnd, nullptr, gInst, nullptr);
    ApplyDwmChrome(gPicker);
    ShowWindow(gPicker, SW_SHOWNOACTIVATE);
    SetForegroundWindow(gPicker);
}

// ----- Main popup window proc -----

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            gDpi = GetDpiForWindow(hwnd);
            if (!gTitleIcon) {
                gTitleIcon = (HICON)LoadImageW(gInst, MAKEINTRESOURCEW(IDI_TRAY), IMAGE_ICON,
                    theme::Scale(20, gDpi), theme::Scale(20, gDpi), LR_DEFAULTCOLOR);
            }
            // Header buttons (ownerdraw)
            gBtnAdd = CreateWindowExW(0, L"BUTTON", L"\uE710",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_BTN_ADD, gInst, nullptr);
            gBtnFormat = CreateWindowExW(0, L"BUTTON",
                settings::Get().use24h ? L"24" : L"12",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_BTN_FORMAT, gInst, nullptr);

            // List
            gList = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS |
                LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_LIST, gInst, nullptr);
            SendMessageW(gList, LB_SETITEMHEIGHT, 0, RowHeightPx());

            // Footer reset
            gBtnReset = CreateWindowExW(0, L"BUTTON", L"Reset",
                WS_CHILD | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_BTN_RESET, gInst, nullptr);

            // Populate list items
            for (size_t i = 0; i < settings::Get().zones.size(); ++i) {
                SendMessageW(gList, LB_ADDSTRING, 0, (LPARAM)L"");
            }

            ApplyDwmChrome(hwnd);
            SetTimer(hwnd, kTimerId, kTickIntervalMs, nullptr);
            return 0;
        }
        case WM_DPICHANGED: {
            gDpi = HIWORD(wp);
            SendMessageW(gList, LB_SETITEMHEIGHT, 0, RowHeightPx());
            RECT* prc = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, prc->left, prc->top,
                prc->right - prc->left, prc->bottom - prc->top, SWP_NOZORDER);
            LayoutChildren(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_SIZE:
            LayoutChildren(hwnd);
            return 0;
        case WM_TIMER:
            if (wp == kTimerId && !gEditInline) InvalidateRect(gList, nullptr, FALSE);
            return 0;
        case WM_APP_START_EDIT:
            StartInlineEdit((int)wp);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            PaintHeaderFooter(hdc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp;
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(theme::CurrentPalette().bg);
            FillRect(hdc, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wp;
            const auto& p = theme::CurrentPalette();
            SetTextColor(hdc, p.text);
            SetBkColor(hdc, p.layer);
            static HBRUSH sBr = nullptr;
            if (sBr) DeleteObject(sBr);
            sBr = CreateSolidBrush(p.layer);
            return (LRESULT)sBr;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
            if (dis->CtlID == IDC_LIST) {
                DrawListItem(dis);
            } else {
                // Owner-draw buttons
                const auto& p = theme::CurrentPalette();
                bool pressed = dis->itemState & ODS_SELECTED;
                COLORREF bg = pressed ? p.controlPressed : p.controlBg;
                theme::FillRoundRect(dis->hDC, dis->rcItem, theme::kRadiusControl, bg);
                theme::DrawRoundedBorder(dis->hDC, dis->rcItem, theme::kRadiusControl, p.stroke);

                wchar_t buf[16]; GetWindowTextW(GetDlgItem(hwnd, dis->CtlID), buf, 16);
                bool isIcon = (buf[0] >= 0xE000);
                HFONT f = isIcon
                    ? theme::GetIconFont(14, gDpi)
                    : theme::GetFont(theme::kTypeBody, true, gDpi);
                theme::DrawTextToken(dis->hDC, buf, dis->rcItem, f, p.text,
                    DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            UINT id = LOWORD(wp);
            UINT code = HIWORD(wp);
            if (id == IDC_BTN_ADD && code == BN_CLICKED) {
                ShowPicker();
                return 0;
            }
            if (id == IDC_BTN_FORMAT && code == BN_CLICKED) {
                auto& s = settings::Get();
                s.use24h = !s.use24h;
                settings::Save();
                SetWindowTextW(gBtnFormat, s.use24h ? L"24" : L"12");
                InvalidateRect(gList, nullptr, FALSE);
                return 0;
            }
            if (id == IDC_BTN_RESET && code == BN_CLICKED) {
                ResetTime();
                return 0;
            }
            if (id == IDC_LIST && code == LBN_DBLCLK) {
                int sel = (int)SendMessageW(gList, LB_GETCURSEL, 0, 0);
                // Defer until the double-click message pump fully unwinds so
                // the trailing WM_LBUTTONUP doesn't steal focus from the edit.
                if (sel >= 0) PostMessageW(hwnd, WM_APP_START_EDIT, sel, 0);
                return 0;
            }
            break;
        }
        case WM_NOTIFY:
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                // Grace period right after show: the shell / tray click can
                // briefly bounce focus, which would otherwise immediately hide.
                if (GetTickCount() - gShownAtTick < kDismissGraceMs) return 0;
                HWND nxt = (HWND)lp;
                // Keep open when activation moves to an owned child popup.
                if (nxt && (nxt == gPicker || nxt == gEditInline ||
                            GetAncestor(nxt, GA_ROOT) == hwnd)) {
                    return 0;
                }
                Hide();
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && wcscmp((wchar_t*)lp, L"ImmersiveColorSet") == 0) {
                theme::InvalidatePaletteCache();
                ApplyDwmChrome(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_TIMECHANGE:
            InvalidateRect(gList, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerId);
            if (gTitleIcon) { DestroyIcon(gTitleIcon); gTitleIcon = nullptr; }
            gHwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// List subclass for single-click close button / drag-reorder.
LRESULT CALLBACK ListSubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                             UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            int idx = (int)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
            if (HIWORD(idx) == 0) {
                int row = LOWORD(idx);
                if (IsPointOnCloseGlyph(x, row)) {
                    auto& zones = settings::Get().zones;
                    if (row < (int)zones.size()) {
                        zones.erase(zones.begin() + row);
                        settings::Save();
                        Refresh();
                    }
                    return 0;
                }
                gDragFrom = row;
                gDragStart.x = x; gDragStart.y = y;
                SetCapture(hwnd);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (GetCapture() == hwnd && gDragFrom >= 0) {
                int y = GET_Y_LPARAM(lp);
                int dy = y - gDragStart.y;
                if (!gDragging && abs(dy) > theme::Scale(6, gDpi)) gDragging = true;
                if (gDragging) {
                    int idx = (int)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0,
                                                MAKELPARAM(gDragStart.x, y));
                    if (HIWORD(idx) == 0) gDragTo = LOWORD(idx);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (GetCapture() == hwnd) ReleaseCapture();
            if (gDragging && gDragFrom >= 0 && gDragTo >= 0 && gDragTo != gDragFrom) {
                auto& zones = settings::Get().zones;
                if (gDragFrom < (int)zones.size() && gDragTo < (int)zones.size()) {
                    auto item = zones[gDragFrom];
                    zones.erase(zones.begin() + gDragFrom);
                    zones.insert(zones.begin() + gDragTo, item);
                    settings::Save();
                    Refresh();
                }
            }
            gDragging = false; gDragFrom = gDragTo = -1;
            break;
        }
        case WM_MOUSELEAVE:
            gDragging = false; gDragFrom = gDragTo = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

} // namespace

void Initialize(HINSTANCE hInst) {
    gInst = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    WNDCLASSEXW pc{};
    pc.cbSize = sizeof(pc);
    pc.lpfnWndProc = PickerProc;
    pc.hInstance = hInst;
    pc.lpszClassName = kPickerClass;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&pc);
}

void Shutdown() {
    if (gHwnd) DestroyWindow(gHwnd);
    gHwnd = nullptr;
}

void EnsureCreated() {
    if (gHwnd) return;
    gHwnd = CreateWindowExW(WS_EX_TOOLWINDOW,
        kClass, L"World Clock",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, 320, 400, nullptr, nullptr, gInst, nullptr);
    SetWindowSubclass(gList, ListSubProc, 1, 0);
}

void Show() {
    EnsureCreated();
    if (!gHwnd) return;
    PositionPopup(gHwnd);
    gShownAtTick = GetTickCount();
    ShowWindow(gHwnd, SW_SHOW);
    // AllowSetForegroundWindow is implicit since we are the initiating process,
    // but foreground lock can still apply right after a tray click — retry once.
    if (!SetForegroundWindow(gHwnd)) {
        DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        DWORD myThread = GetCurrentThreadId();
        if (fgThread && fgThread != myThread) {
            AttachThreadInput(myThread, fgThread, TRUE);
            SetForegroundWindow(gHwnd);
            AttachThreadInput(myThread, fgThread, FALSE);
        }
    }
    SetActiveWindow(gHwnd);
    SetFocus(gHwnd);
    InvalidateRect(gHwnd, nullptr, TRUE);
}

void Hide() {
    if (gHwnd) ShowWindow(gHwnd, SW_HIDE);
    CancelInlineEdit();
}

void Toggle() {
    if (gHwnd && IsWindowVisible(gHwnd)) Hide();
    else Show();
}

void Refresh() {
    if (!gHwnd) return;
    // Rebuild list entries to match settings.
    SendMessageW(gList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < settings::Get().zones.size(); ++i) {
        SendMessageW(gList, LB_ADDSTRING, 0, (LPARAM)L"");
    }
    PositionPopup(gHwnd);
    LayoutChildren(gHwnd);
    SetWindowTextW(gBtnFormat, settings::Get().use24h ? L"24" : L"12");
    InvalidateRect(gHwnd, nullptr, TRUE);
}

void ApplyAlwaysOnTop() {
    if (!gHwnd) return;
    SetWindowPos(gHwnd,
        settings::Get().alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ResetTime() {
    clock_engine::Reset();
    if (gHwnd) {
        LayoutChildren(gHwnd);
        InvalidateRect(gHwnd, nullptr, TRUE);
    }
}

} // namespace popup
