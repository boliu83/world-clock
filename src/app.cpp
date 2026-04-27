#include "resource.h"
#include "settings.h"
#include "theme.h"
#include "tz.h"
#include "tray.h"
#include "popup.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\WorldClock.Singleton.Mutex.v1";
constexpr wchar_t kWakeMsgName[] = L"WorldClock.Wake";

HANDLE gMutex = nullptr;

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // Single instance.
    gMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!gMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        UINT wake = RegisterWindowMessageW(kWakeMsgName);
        if (wake) SendNotifyMessageW(HWND_BROADCAST, wake, 0, 0);
        if (gMutex) CloseHandle(gMutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    settings::Load();
    tz::Initialize();

    // Register a broadcast wake message listener by subclassing the tray owner.
    UINT wakeMsg = RegisterWindowMessageW(kWakeMsgName);

    popup::Initialize(hInst);
    HWND owner = tray::Create(hInst);
    (void)owner;

    // Apply startup preference at launch (keeps it idempotent).
    settings::ApplyRunAtStartup(settings::Get().runAtStartup);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == wakeMsg) {
            popup::Show();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    popup::Shutdown();
    tray::Destroy();
    tz::Shutdown();
    theme::ReleaseCaches();
    settings::Save();
    if (gMutex) { ReleaseMutex(gMutex); CloseHandle(gMutex); }
    return static_cast<int>(msg.wParam);
}
