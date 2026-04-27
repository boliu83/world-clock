#include "tray.h"
#include "resource.h"
#include "settings.h"
#include "popup.h"
#include <shellapi.h>
#include <algorithm>
#include <string>

namespace tray {

namespace {

constexpr wchar_t kOwnerClass[] = L"WorldClockOwner";
constexpr UINT   kTrayUid       = 1;

HWND  gOwner   = nullptr;
HICON gIcon    = nullptr;
UINT  gTaskbarCreatedMsg = 0;

HICON LoadTrayIcon() {
    // Prefer the embedded resource icon (our custom clock face).
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HICON ic = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_TRAY),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED);
    if (ic) return ic;
    // Fallback: Windows clock glyph from imageres.dll.
    wchar_t sysRoot[MAX_PATH]; GetSystemDirectoryW(sysRoot, MAX_PATH);
    std::wstring imageres = std::wstring(sysRoot) + L"\\imageres.dll";
    for (int idx : {-15, -110, -24}) {
        HICON ex = ExtractIconW(hInst, imageres.c_str(), idx);
        if (ex && ex != (HICON)1) return ex;
    }
    return LoadIconW(nullptr, IDI_APPLICATION);
}

void AddIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = gOwner;
    nid.uID    = kTrayUid;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon  = gIcon;
    wcscpy_s(nid.szTip, L"World Clock");
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = gOwner;
    nid.uID = kTrayUid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU m = CreatePopupMenu();
    HMENU heightMenu = CreatePopupMenu();
    auto& s = settings::Get();
    int popupRows = std::clamp(s.popupHeightRows, 0, 20);
    AppendMenuW(m, MF_STRING, IDM_SHOW, L"Show world clock");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (s.use24h ? MF_CHECKED : 0),
                IDM_TOGGLE_FORMAT, L"Use 24-hour time");
    AppendMenuW(m, MF_STRING | (s.showSeconds ? MF_CHECKED : 0),
                IDM_TOGGLE_SECONDS, L"Show seconds");
    AppendMenuW(m, MF_STRING | (s.alwaysOnTop ? MF_CHECKED : 0),
                IDM_ALWAYS_ON_TOP, L"Always on top");
    AppendMenuW(m, MF_STRING | (s.runAtStartup ? MF_CHECKED : 0),
                IDM_RUN_AT_STARTUP, L"Run at startup");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(heightMenu, MF_STRING | (popupRows == 0 ? MF_CHECKED : 0),
                IDM_POPUP_HEIGHT_BASE, L"0 rows (auto)");
    for (int rows = 1; rows <= 20; ++rows) {
        wchar_t label[32];
        swprintf_s(label, L"%d %s", rows, rows == 1 ? L"row" : L"rows");
        AppendMenuW(heightMenu, MF_STRING | (popupRows == rows ? MF_CHECKED : 0),
                    IDM_POPUP_HEIGHT_BASE + rows, label);
    }
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(heightMenu), L"Popup height");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_RESET_TIME, L"Reset to current time");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");
    // Required for right-click menus to dismiss properly.
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(m, TPM_RIGHTBUTTON, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(m);
}

} // namespace

LRESULT CALLBACK OwnerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == gTaskbarCreatedMsg) {
        AddIcon();
        return 0;
    }
    switch (msg) {
        case WM_APP_TRAY: {
            UINT evt = LOWORD(lp);
            // With NOTIFYICON_VERSION_4, NIN_SELECT is the canonical left-click
            // event. WM_LBUTTONUP may also fire on some Explorer builds, which
            // would Toggle() twice and instantly re-hide the popup.
            if (evt == NIN_SELECT) {
                popup::Toggle();
            } else if (evt == WM_CONTEXTMENU || evt == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                ShowContextMenu(hwnd, pt);
            }
            return 0;
        }
        case WM_APP_SHOW_POPUP:
            popup::Show();
            return 0;
        case WM_COMMAND:
            HandleCommand(hwnd, LOWORD(wp));
            return 0;
        case WM_DESTROY:
            RemoveIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND Create(HINSTANCE hInst) {
    gTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OwnerWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kOwnerClass;
    RegisterClassExW(&wc);

    gOwner = CreateWindowExW(0, kOwnerClass, L"WorldClock",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    gIcon = LoadTrayIcon();
    AddIcon();
    return gOwner;
}

void Destroy() {
    if (gOwner) {
        RemoveIcon();
        DestroyWindow(gOwner);
        gOwner = nullptr;
    }
    if (gIcon) { DestroyIcon(gIcon); gIcon = nullptr; }
}

bool GetIconRect(RECT& outScreenRect) {
    NOTIFYICONIDENTIFIER id{};
    id.cbSize = sizeof(id);
    id.hWnd = gOwner;
    id.uID = kTrayUid;
    RECT r{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &r))
        && (r.right > r.left) && (r.bottom > r.top)) {
        outScreenRect = r;
        return true;
    }
    return false;
}

void HandleCommand(HWND hwnd, UINT cmdId) {
    auto& s = settings::Get();
    if (cmdId >= IDM_POPUP_HEIGHT_BASE && cmdId <= IDM_POPUP_HEIGHT_MAX) {
        s.popupHeightRows = static_cast<int>(cmdId - IDM_POPUP_HEIGHT_BASE);
        settings::Save();
        popup::Refresh();
        return;
    }
    switch (cmdId) {
        case IDM_SHOW:
            popup::Show();
            break;
        case IDM_TOGGLE_FORMAT:
            s.use24h = !s.use24h;
            settings::Save();
            popup::Refresh();
            break;
        case IDM_TOGGLE_SECONDS:
            s.showSeconds = !s.showSeconds;
            settings::Save();
            popup::Refresh();
            break;
        case IDM_ALWAYS_ON_TOP:
            s.alwaysOnTop = !s.alwaysOnTop;
            settings::Save();
            popup::ApplyAlwaysOnTop();
            break;
        case IDM_RUN_AT_STARTUP:
            s.runAtStartup = !s.runAtStartup;
            settings::ApplyRunAtStartup(s.runAtStartup);
            settings::Save();
            break;
        case IDM_RESET_TIME:
            popup::ResetTime();
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
    }
}

} // namespace tray
