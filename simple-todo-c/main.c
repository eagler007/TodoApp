#include "app.h"
#include "app_ui.h"
#include "ui.h"
#include "util.h"
#include <wchar.h>

/*
 * Tab / Shift+Tab 在输入框之间循环。
 *
 * 为什么自己实现、不交给对话框管理器：本项目的输入框是「多行 EDIT 伪装单行」
 * （为了文字垂直居中，见 app_ui.c 的 make_edit），多行 EDIT 对 Tab 的
 * WM_GETDLGCODE 会索要这个键，IsDialogMessage 拿不到，Tab 就死在控件里
 * （表现为「Tab 键无效」，且 WM_CHAR 还被 edit_vc_sub 吞掉，连制表符都不进文本）。
 *
 * 只串 EDIT、跳过 Button 和自绘下拉框：后两者拿到焦点也没有任何视觉反馈，
 * 跳进去等于「按了没反应」，反而更让人困惑。
 */
static void focus_next_edit(int back, int select_all) {
    HWND cur = GetFocus();
    if (!cur) return;
    HWND box = GetParent(cur);          /* 输入框都直接挂在面板 / 倒计时条下 */
    if (!box) return;

    HWND list[64];
    int n = 0;
    for (HWND h = GetWindow(box, GW_CHILD); h && n < 64; h = GetWindow(h, GW_HWNDNEXT)) {
        if (!IsWindowVisible(h) || !IsWindowEnabled(h)) continue;
        if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_TABSTOP)) continue;
        WCHAR cls[16];
        if (!GetClassNameW(h, cls, 16)) continue;
        if (_wcsicmp(cls, L"Edit") != 0) continue;
        list[n++] = h;
    }
    if (n == 0) return;

    int idx = -1;
    for (int i = 0; i < n; i++) if (list[i] == cur) { idx = i; break; }
    int next = (idx < 0) ? (back ? n - 1 : 0) : (idx + (back ? -1 : 1) + n) % n;
    if (next == idx) return;            /* 容器里只有一个输入框，保持原行为 */

    HWND t = list[next];
    SetFocus(t);
    /* 倒计时条（纯数字，按 Tab 就是来改的）→ 全选，进去直接覆盖；
       其余输入框仍落末尾，避免搜索词/备注这类长文本被一击整段盖掉 */
    if (select_all)
        SendMessageW(t, EM_SETSEL, 0, (LPARAM)-1);
    else
        SendMessageW(t, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    (void)hPrevInstance;

    /* --tray：开机自启专用，静默进托盘不弹主窗口。
       提醒到点由 Reminder_Fire 弹置顶提示框，隐藏状态下一样收得到。 */
    int start_hidden = (lpCmdLine && strstr(lpCmdLine, "--tray")) ? 1 : 0;
    if (start_hidden) nShowCmd = SW_HIDE;

    /* 单实例：两个进程同时写同一份 JSON 会互相覆盖 */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\TodoApp.SingleInstance");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        /* 自启复用已有实例时不能把窗口拽出来：开机那一刻本来就是要它安静待在托盘里，
           这里一 SW_RESTORE 就成了「每次开机都莫名弹一下」 */
        if (!start_hidden) {
            HWND old = FindWindowW(APP_NAME, NULL);
            if (old) {
                if (IsIconic(old)) ShowWindow(old, SW_RESTORE);
                SetForegroundWindow(old);
            }
        }
        CloseHandle(hMutex);
        return 0;
    }

    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&icex);

    AppState app;
    memset(&app, 0, sizeof(app));

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, L"MAINICON");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_NAME;
    wc.lpszMenuName = NULL;
    /* CS_DBLCLKS：标题栏的天气要双击才能改城市，没有它就收不到 WM_LBUTTONDBLCLK */
    wc.style = CS_DROPSHADOW | CS_DBLCLKS;   /* 无边框窗口靠 CS_DROPSHADOW 保留投影 */
    RegisterClassW(&wc);

    WNDCLASSW panel_wc = {0};
    panel_wc.lpfnWndProc = PanelWndProc;
    panel_wc.hInstance = hInstance;
    panel_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    panel_wc.hbrBackground = NULL;
    /* 四象限里的条目要双击编辑：没有 CS_DBLCLKS 就收不到 WM_LBUTTONDBLCLK */
    panel_wc.style = CS_DBLCLKS;
    panel_wc.lpszClassName = TODO_PANEL_CLASS;
    RegisterClassW(&panel_wc);

    Ui_Init();

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, APP_NAME, APP_TITLE,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
        WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        MulDiv(APP_WIDTH, Ui_Dpi(), 96), MulDiv(APP_HEIGHT, Ui_Dpi(), 96),
        NULL, NULL, hInstance, &app);

    if (!hwnd) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    ShowWindow(hwnd, nShowCmd);
    UpdateWindow(hwnd);
    Ui_ApplyTheme(&app);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        /* 快捷键要先于 IsDialogMessage 拦下来，否则会被对话框导航逻辑吃掉 */
        if (msg.message == WM_KEYDOWN) {
            /* Tab 必须最先拦：交给 IsDialogMessage 会被多行 EDIT 的
               WM_GETDLGCODE 挡下来，落到控件里又被 WM_CHAR 吞掉 */
            if (msg.wParam == VK_TAB) {
                /* 焦点落在倒计时条里就全选，其余地方保持「光标到末尾」 */
                HWND hBar = (HWND)GetPropW(hwnd, L"TimerBarHWND");
                HWND f = GetFocus();
                focus_next_edit((GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0,
                                hBar && f && IsChild(hBar, f));
                continue;
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                UINT c = (UINT)msg.wParam;
                if (c == 'N' || c == 'F' || c == 'D') {
                    PostMessageW(hwnd, WM_APP + 6, c, 0);
                    continue;
                }
            } else if (msg.wParam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_APP + 6, VK_ESCAPE, 0);
                continue;
            }
        }
        HWND hPanel = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        HWND hTimerBar = (HWND)GetPropW(hwnd, L"TimerBarHWND");
        if (!IsDialogMessageW(hPanel ? hPanel : hwnd, &msg) &&
            !IsDialogMessageW(hTimerBar ? hTimerBar : hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (hMutex) CloseHandle(hMutex);
    return (int)msg.wParam;
}
