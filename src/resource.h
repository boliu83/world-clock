#pragma once

// Resource IDs
#define IDI_TRAY                101

// Tray notification message
#define WM_APP_TRAY             (WM_APP + 1)
// Posted by secondary instance to foreground the popup
#define WM_APP_SHOW_POPUP       (WM_APP + 2)
// 1-Hz tick to repaint popup list
#define WM_APP_TICK             (WM_APP + 3)
// Deferred start of inline-edit (wParam = row index)
#define WM_APP_START_EDIT       (WM_APP + 4)

// Tray context menu commands
#define IDM_SHOW                40001
#define IDM_EXIT                40002
#define IDM_ALWAYS_ON_TOP       40003
#define IDM_RUN_AT_STARTUP      40004
#define IDM_TOGGLE_FORMAT       40005
#define IDM_TOGGLE_SECONDS      40006
#define IDM_RESET_TIME          40007
#define IDM_POPUP_HEIGHT_BASE   40100
#define IDM_POPUP_HEIGHT_MAX    40120

// Popup child control IDs
#define IDC_LIST                50001
#define IDC_BTN_ADD             50002
#define IDC_BTN_FORMAT          50003
#define IDC_BTN_SETTINGS        50004
#define IDC_BTN_RESET           50005
#define IDC_EDIT_INLINE         50006

// Add-picker IDs
#define IDC_PICKER_SEARCH       60001
#define IDC_PICKER_LIST         60002
