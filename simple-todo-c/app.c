#include "app.h"
#include "app_ui.h"
#include "dlg.h"
#include "menu.h"
#include "ui.h"
#include "theme.h"
#include "storage.h"
#include "util.h"
#include "calendar.h"
#include "pie.h"
#include "trend.h"
#include "weather.h"
#include "startup.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

static HFONT g_font = NULL;

/* 这几个 helper 定义在文件后半段，前面的新代码要先用到 */
static AppState* dlg_of_app(HWND h);
static void lv_cols(HWND lv, const WCHAR* const* names, const int* widths,
                    const int* rights, int n);

static HFONT GetDefaultFont(int pt) {
    if (pt == 10 && g_font) return g_font;
    LOGFONTW lf = {0};
    lf.lfHeight = -MulDiv(pt, 96, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = GB2312_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy(lf.lfFaceName, L"Microsoft YaHei");
    HFONT hf = CreateFontIndirectW(&lf);
    if (!hf) {
        NONCLIENTMETRICSW ncm = {sizeof(ncm)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        hf = CreateFontIndirectW(&ncm.lfMenuFont);
    }
    if (pt == 10) g_font = hf;
    return hf;
}

static void SetCtrlFont(HWND ctrl, int pt) {
    HFONT hf = GetDefaultFont(pt);
    if (hf) SendMessage(ctrl, WM_SETFONT, (WPARAM)hf, 1);
}

/*
 * 这里不能用 swprintf(L"%s\\...")：mingw 的 swprintf 在部分运行时（msvcrt 版）
 * 把 %s 当窄字符串解释，WCHAR* 会被读成 "C:" 这种半截路径，数据文件全部读写失败。
 * 一律用 wcscpy/wcscat 拼接，与编译器/运行时无关。
 */
static void path_join(WCHAR* out, const WCHAR* dir, const WCHAR* file) {
    wcsncpy(out, dir, MAX_PATH - 1);
    out[MAX_PATH - 1] = 0;
    if (wcslen(out) + wcslen(file) < MAX_PATH - 1)
        wcscat(out, file);
}

static void CreateAppDataPath(AppState* app, HINSTANCE hInst) {
    WCHAR appdata[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) {
        path_join(app->data_dir, appdata, L"\\TodoApp");
    } else {
        GetModuleFileNameW(hInst, app->data_dir, MAX_PATH);
        wchar_t* p = wcsrchr(app->data_dir, L'\\');
        if (p) *p = L'\0';
    }
    CreateDirectoryW(app->data_dir, NULL);
    path_join(app->todo_path,   app->data_dir, L"\\todos.json");
    path_join(app->acct_path,   app->data_dir, L"\\accounts.json");
    path_join(app->config_path, app->data_dir, L"\\config.ini");
    path_join(app->budget_path, app->data_dir, L"\\budgets.json");
    path_join(app->backup_dir,  app->data_dir, L"\\backups");
    CreateDirectoryW(app->backup_dir, NULL);
}

/* 读失败时列表会是空的，这时绝不能落盘 —— 一落盘就把原文件覆盖掉了 */
static void LoadData(AppState* app) {
    char pathA[MAX_PATH];

    wchar_to_utf8(app->todo_path, pathA, MAX_PATH);
    int r1 = storage_load_todos(pathA, &app->todos);
    for (int i = 0; i < app->todos.count; i++) {
        if (app->todos.items[i].id >= app->next_todo_id)
            app->next_todo_id = app->todos.items[i].id + 1;
    }

    wchar_to_utf8(app->acct_path, pathA, MAX_PATH);
    int r2 = storage_load_accounts(pathA, &app->accounts);
    for (int i = 0; i < app->accounts.count; i++) {
        if (app->accounts.items[i].id >= app->next_acct_id)
            app->next_acct_id = app->accounts.items[i].id + 1;
    }

    wchar_to_utf8(app->budget_path, pathA, MAX_PATH);
    storage_load_budgets(pathA, &app->budgets);   /* 预算坏了不致命，按空处理 */

    if (r1 < 0 || r2 < 0) {
        app->read_only   = 1;
        app->data_broken = 1;
    }

    /* 软删除超过 30 天的彻底清掉 */
    if (!app->read_only) {
        int n = acct_list_purge_deleted(&app->accounts, 30);
        n += todo_list_purge_deleted(&app->todos, 30);
        (void)n;
    }
}

static void SaveTodos(AppState* app) {
    if (app->read_only) return;
    char pathA[MAX_PATH];
    wchar_to_utf8(app->todo_path, pathA, MAX_PATH);
    if (!storage_save_todos(pathA, &app->todos)) return;
    char bak[MAX_PATH];
    wchar_to_utf8(app->backup_dir, bak, MAX_PATH);
    storage_daily_backup(pathA, bak, 30);
}

static void SaveAccounts(AppState* app) {
    if (app->read_only) return;
    char pathA[MAX_PATH];
    wchar_to_utf8(app->acct_path, pathA, MAX_PATH);
    if (!storage_save_accounts(pathA, &app->accounts)) return;
    char bak[MAX_PATH];
    wchar_to_utf8(app->backup_dir, bak, MAX_PATH);
    storage_daily_backup(pathA, bak, 30);
}

static void SaveBudgets(AppState* app) {
    if (app->read_only) return;
    char pathA[MAX_PATH];
    wchar_to_utf8(app->budget_path, pathA, MAX_PATH);
    storage_save_budgets(pathA, &app->budgets);
}

/* 只读模式下的统一拦截：第一次弹一次提示，之后静默 */
static int WriteBlocked(AppState* app) {
    static int warned = 0;
    if (!app->read_only) return 0;
    if (!warned) {
        warned = 1;
        Ui_MsgBox(GetAncestor(app->hTodoPanel, GA_ROOT), L"数据只读",
                  L"数据文件读取失败，已进入只读模式，不会写入任何改动。\n"
                  L"请先修复数据文件，或从 backups 目录里恢复一份。", MSG_ERROR);
    }
    return 1;
}

/* 列表标题后面的周期后缀：· 每天 / · 每周三 / · 每月 15 号 */
/* 重复后缀 / 行内小字在 todo.c 里（主界面卡片和标题拼接共用一份文案） */

/*
 * 把待办按四象限分到 4 个方块里。
 * 块内顺序：未完成在前、完成的沉底，同状态保持录入顺序（插入排序是稳定的）。
 */
static void BuildTodoQuad(AppState* app, const char* filter) {
    for (int q = 0; q < TODO_QUAD_COUNT; q++) {
        app->quad_n[q] = 0;
        app->quad_total[q] = 0;
    }

    for (int i = 0; i < app->todos.count; i++) {
        Todo* t = &app->todos.items[i];
        if (t->deleted) continue;                 /* 软删除的不显示，「最近删除」里看 */
        if (filter && filter[0]) {
            if (!strstr(t->title, filter) && !strstr(t->content, filter))
                continue;
        }
        int q = TODO_PRIO_QUAD(t->priority);
        if (q < 0) q = TODO_PRIO_QUAD(TODO_PRIO_NONE);
        if (q >= TODO_QUAD_COUNT) q = 0;

        app->quad_total[q]++;
        if (app->quad_n[q] >= QUAD_MAX_ROWS) continue;   /* 超出上限的只计数，不显示 */

        /* 插入排序：已完成的往后沉 */
        int k = app->quad_n[q]++;
        while (k > 0) {
            Todo* prev = todo_list_find(&app->todos, app->quad_ids[q][k - 1]);
            if (!prev || !prev->completed) break;
            if (t->completed) break;
            app->quad_ids[q][k] = app->quad_ids[q][k - 1];
            k--;
        }
        app->quad_ids[q][k] = t->id;
    }

    /* 数据变少时把滚动偏移收回来，否则会停在一片空白上 */
    for (int q = 0; q < TODO_QUAD_COUNT; q++) {
        int maxs = app->quad_n[q] - app->quad_rows_vis;
        if (maxs < 0) maxs = 0;
        if (app->quad_scroll[q] > maxs) app->quad_scroll[q] = maxs;
        if (app->quad_scroll[q] < 0) app->quad_scroll[q] = 0;
    }
}

#define ACCT_CC_MAX 64
#define ACCT_CI_NONE 0xF      /* lParam 高位里的「不画色块」标记 */

/* 分类 -> 色板下标。
 * 1) 常见分类走固定预设（餐饮永远橙、购物永远蓝），与出现顺序、
 *    与其他分类是否同屏都无关，颜色绝对稳定；
 * 2) 其余分类按名称升序做哈希，撞色时线性探测下一个空位，
 *    排序保证同一组分类在任何视图里分到的颜色完全一致。 */
static void cat_colors_build(char cats[][ACCT_CAT_MAX], int n, int* out) {
    int ord[ACCT_CC_MAX];
    int used[UI_CAT_PAL_N];
    memset(used, 0, sizeof(used));

    /* 预设分类先占位 */
    for (int i = 0; i < n; i++) {
        int p = Ui_CatPresetIndex(cats[i]);
        out[i] = -1;
        if (p >= 0 && p < UI_CAT_PAL_N && !used[p]) {
            used[p] = 1;
            out[i] = p;
        }
    }

    /* 剩下的按名称升序哈希分配 */
    int m = 0;
    for (int i = 0; i < n; i++) if (out[i] < 0) ord[m++] = i;
    for (int i = 1; i < m; i++) {
        int k = ord[i], j = i - 1;
        while (j >= 0 && strcmp(cats[ord[j]], cats[k]) > 0) { ord[j + 1] = ord[j]; j--; }
        ord[j + 1] = k;
    }
    for (int t = 0; t < m; t++) {
        int k = ord[t];
        unsigned h = 2166136261u;
        for (const char* q = cats[k]; *q; q++) { h ^= (unsigned char)*q; h *= 16777619u; }
        int ci = (int)(h % UI_CAT_PAL_N);
        for (int s = 0; s < UI_CAT_PAL_N && used[ci]; s++) ci = (ci + 1) % UI_CAT_PAL_N;
        used[ci] = 1;
        out[k] = ci;
    }
}

static void UpdateAccountListView(HWND hList, AccountList* list, const char* month,
                                  const char* filter) {
    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);
    acct_list_sort_by_date_desc(list);
    WCHAR wbuf[512];
    char amt[32];

    /* 先过一遍筛出要显示的行，按「首次出现顺序」给每个分类分配一个颜色位，
     * 保证同屏内各分类颜色互不相同（超过色板数量才循环）。 */
    char cats[ACCT_CC_MAX][ACCT_CAT_MAX];
    int  catcol[ACCT_CC_MAX];   /* 每个分类分到的色板下标 */
    int  ncat = 0;
    int  nrow = 0;
    int* rows = list->count > 0 ? (int*)malloc(sizeof(int) * (size_t)list->count) : NULL;

    for (int i = 0; rows && i < list->count; i++) {
        AccountRecord* r = &list->items[i];
        if (r->deleted) continue;                 /* 软删除的不进列表 */
        /* 只显示当前月份的数据；acct_month 为空时才退回「全部」 */
        if (month && month[0]) {
            char im[16];
            get_month_from_date(r->date, im, sizeof(im));
            if (strcmp(im, month) != 0) continue;
        }
        if (filter && filter[0]) {
            if (!strstr(r->date, filter) && !strstr(r->category, filter) &&
                !strstr(r->note, filter))
                continue;
        }
        rows[nrow++] = i;

        int found = 0;
        for (int k = 0; k < ncat; k++) {
            if (strcmp(cats[k], r->category) == 0) { found = 1; break; }
        }
        if (!found && ncat < ACCT_CC_MAX) {
            strncpy(cats[ncat], r->category, sizeof(cats[0]) - 1);
            cats[ncat][sizeof(cats[0]) - 1] = 0;
            ncat++;
        }
    }

    cat_colors_build(cats, ncat, catcol);

    for (int j = 0; j < nrow; j++) {
        AccountRecord* r = &list->items[rows[j]];
        int ci = 0;
        for (int k = 0; k < ncat; k++) {
            if (strcmp(cats[k], r->category) == 0) { ci = catcol[k]; break; }
        }

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = (int)SendMessageW(hList, LVM_GETITEMCOUNT, 0, 0);
        /* 低 24 位 id，25-28 位分类色号，29 位标记收入行（自绘时按它换色） */
        int inc = acct_is_income(r);
        lvi.lParam = (LPARAM)((((DWORD)ci & 0xF) << 25) |
                              ((DWORD)(inc ? 1 : 0) << 29) |
                              ((DWORD)r->id & 0x00FFFFFFu));

        lvi.iSubItem = 0;
        utf8_to_wchar(r->date, wbuf, 512);
        lvi.pszText = wbuf;
        SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 1;
        lvi.mask = LVIF_TEXT;
        utf8_to_wchar(r->category, wbuf, 512);
        lvi.pszText = wbuf;
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 2;
        lvi.mask = LVIF_TEXT;
        {
            char mv[64];
            fmt_money_utf8(mv, sizeof(mv), r->amount, 2);
            snprintf(amt, sizeof(amt), "%s%s", inc ? "+" : "-", mv);
        }
        utf8_to_wchar(amt, wbuf, 512);
        lvi.pszText = wbuf;
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 3;
        lvi.mask = LVIF_TEXT;
        utf8_to_wchar(r->note, wbuf, 512);
        lvi.pszText = wbuf;
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }
    free(rows);
}

static void RefreshTodoTab(AppState* app) {
    if (app->current_tab != 0) return;
    char filter[128] = {0};
    if (app->hTodoSearch) {
        WCHAR wfilter[128];
        GetWindowTextW(app->hTodoSearch, wfilter, 128);
        wchar_to_utf8(wfilter, filter, sizeof(filter));
    }
    BuildTodoQuad(app, filter);
    if (app->hTodoPanel) InvalidateRect(app->hTodoPanel, NULL, FALSE);
}

static void RefreshAccountTab(AppState* app) {
    if (app->current_tab != 1) return;
    char filter[128] = {0};
    if (app->hAcctSearch) {
        WCHAR wfilter[128];
        GetWindowTextW(app->hAcctSearch, wfilter, 128);
        wchar_to_utf8(wfilter, filter, sizeof(filter));
    }
    UpdateAccountListView(app->hAcctList, &app->accounts, app->acct_month, filter);
    /* 顶上的「本月支出」汇总卡也要跟着重算 */
    if (app->hAcctPanel) InvalidateRect(app->hAcctPanel, NULL, FALSE);
}

static void UpdateStatusBar(AppState* app) {
    char date_str[32], weekday[16], time_str[16], full[64];
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    strftime(date_str, sizeof(date_str), DATE_FORMAT, tm_now);
    strftime(time_str, sizeof(time_str), "%H:%M", tm_now);
    get_weekday_str(weekday, sizeof(weekday));
    snprintf(full, sizeof(full), "%s %s %s", date_str, weekday, time_str);
    utf8_to_wchar(full, app->clock_text, 64);

    HWND top = app->hTodoPanel ? GetParent(app->hTodoPanel) : NULL;
    if (top) {
        RECT rc = { 0, 0, Ui_S(420), Ui_S(UI_TOPBAR_H) };
        InvalidateRect(top, &rc, FALSE);
    }
}

static void SwitchTab(HWND hwnd, AppState* app, int tab_idx) {
    app->current_tab = tab_idx;
    if (tab_idx == 0) {
        ShowWindow(app->hTodoPanel, SW_SHOW);
        ShowWindow(app->hAcctPanel, SW_HIDE);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app->hTodoPanel);
        RefreshTodoTab(app);
    } else {
        ShowWindow(app->hTodoPanel, SW_HIDE);
        ShowWindow(app->hAcctPanel, SW_SHOW);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app->hAcctPanel);
        RefreshAccountTab(app);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void ShowTrayIcon(HWND hwnd, HINSTANCE hInst) {
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TRAY_MSG;
    nid.hIcon = LoadIconW(hInst, L"MAINICON");
    wcscpy(nid.szTip, L"TodoApp");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void UpdateTimerDisplayLabel(AppState* app) {
    Ui_RefreshTimerBar(app);
}

static void StartTimerCountdown(AppState* app) {
    if (app->timer_paused) {
        app->timer_paused = 0;
        app->timer_running = 1;
        return;
    }
    WCHAR wbuf[8];
    int hours = 0, minutes = 0, seconds = 0;
    GetWindowTextW(app->hTimerHour, wbuf, 8); hours = _wtoi(wbuf);
    GetWindowTextW(app->hTimerMin, wbuf, 8);  minutes = _wtoi(wbuf);
    GetWindowTextW(app->hTimerSec, wbuf, 8);  seconds = _wtoi(wbuf);
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 ||
        seconds < 0 || seconds > 59) {
        Ui_MsgBox(GetAncestor(app->hTimerBar, GA_ROOT), L"时间超出范围",
                  L"小时请填 0–23，分和秒请填 0–59。", MSG_WARN);
        return;
    }
    int total = hours * 3600 + minutes * 60 + seconds;
    if (total <= 0) {
        Ui_MsgBox(GetAncestor(app->hTimerBar, GA_ROOT), L"提示", L"请设置时间。", MSG_WARN);
        return;
    }
    app->timer_seconds = total;
    app->timer_total = total;
    app->timer_running = 1;
    app->timer_paused = 0;
    UpdateTimerDisplayLabel(app);
}

static void PauseTimer(AppState* app) {
    app->timer_paused = 1;
}

/*
 * 倒计时归零的提醒。
 *
 * 规矩：**只弹提示框，不碰主界面**。主窗口最小化就继续最小化、藏在托盘就继续藏着，
 * 不要因为它到点了就把人从别的程序里拽回来（原来这里 SW_RESTORE + SetForegroundWindow
 * 会把整个主界面弹到最前，等于强抢前台）。
 *
 * 为什么不用 Windows 通知栏（Shell_NotifyIcon + NIF_INFO 气泡）：那玩意儿由系统代发，
 * 专注助手/勿扰、通知被关、explorer 重启过、图标刚加还没注册上……任一情况都会静默吞掉，
 * 用户完全不知道倒计时结束了。所以这里走应用自己的提示框（Ui_MsgBoxFront）：
 * 置顶 + 抢前台 + 闪边框，不依赖系统通知设置。
 */
static void NotifyTimerDone(HWND hwnd, AppState* app) {
    if (!hwnd) return;

    /* 任务栏闪烁：窗口还可见（含最小化）时闪 5 下就停，藏在托盘时是空操作，无害。
       ⚠️ 别用 FLASHW_TIMERNOFG —— 它的意思是「一直闪到窗口成为前台」，
       而现在我们故意不激活主窗口，那样任务栏会闪到用户手动点开为止。 */
    {
        FLASHWINFO fw;
        memset(&fw, 0, sizeof(fw));
        fw.cbSize = sizeof(fw);
        fw.hwnd = hwnd;
        fw.dwFlags = FLASHW_ALL;
        fw.uCount = 5;
        fw.dwTimeout = 0;
        FlashWindowEx(&fw);
    }
    MessageBeep(MB_ICONEXCLAMATION);

    WCHAR msg[160];
    int t = app->timer_total;
    if (t >= 3600) {
        int hh = t / 3600, mm = (t % 3600) / 60;
        if (mm > 0) _snwprintf(msg, 160, L"%d 小时 %d 分钟的倒计时已结束。", hh, mm);
        else        _snwprintf(msg, 160, L"%d 小时的倒计时已结束。", hh);
    } else if (t >= 60) {
        int mm = t / 60, ss = t % 60;
        if (ss > 0) _snwprintf(msg, 160, L"%d 分 %d 秒的倒计时已结束。", mm, ss);
        else        _snwprintf(msg, 160, L"%d 分钟的倒计时已结束。", mm);
    } else if (t > 0) {
        _snwprintf(msg, 160, L"%d 秒的倒计时已结束。", t);
    } else {
        _snwprintf(msg, 160, L"倒计时已结束。");
    }
    msg[159] = 0;

    Ui_MsgBoxFront(hwnd, L"倒计时结束", msg, MSG_INFO);
}

static void ResetTimer(AppState* app) {
    app->timer_running = 0;
    app->timer_paused = 0;
    app->timer_seconds = 0;
    app->timer_total = 0;
    SetWindowTextW(app->hTimerHour, L"");
    SetWindowTextW(app->hTimerMin, L"");
    SetWindowTextW(app->hTimerSec, L"");
    UpdateTimerDisplayLabel(app);
}

static int parse_date_from_iso(const char* iso_str, struct tm* tm_out) {
    if (!iso_str || strlen(iso_str) < 19) return 0;
    memset(tm_out, 0, sizeof(struct tm));
    tm_out->tm_year = atoi(iso_str) - 1900;
    tm_out->tm_mon = atoi(iso_str + 5) - 1;
    tm_out->tm_mday = atoi(iso_str + 8);
    tm_out->tm_hour = atoi(iso_str + 11);
    tm_out->tm_min = atoi(iso_str + 14);
    tm_out->tm_sec = atoi(iso_str + 17);
    return 1;
}

/*
 * 提醒到点。
 *
 * 老实现（托盘气泡）有两个硬伤，都实际发生过：
 *   1. 一轮里多条同时到点，连续 Shell_NotifyIcon(NIM_MODIFY) 会互相覆盖，
 *      气泡一次只显示得出一条 → 用户只看到最后一条；
 *   2. 每条命中后立刻把 reminder 清空并写盘 → 没看到的那几条连数据都没了，永久丢失。
 * 再加上气泡本身由系统代发，专注助手 / 勿扰 / 通知被关 / explorer 重启过都会静默吞掉，
 * 一点痕迹都没有（跟倒计时那个提醒是同一个坑）。
 *
 * 现在：先把所有到点的收集起来 → 汇总成一个**置顶提示框** → 用户点了「知道了」才清空。
 * at_boot = 1 表示这是启动时补报「程序没开的时候错过的」，文案与标题会区分开。
 */
#define REMINDER_SHOW_MAX 8      /* 正文最多列这么多条，再多就「…等 N 条」 */

static void Reminder_Fire(AppState* app, HWND hwnd, int at_boot) {
    if (!app || !hwnd) return;

    time_t now = time(NULL);
    int total = 0;

    WCHAR body[1400];
    WCHAR first_title[TODO_TITLE_MAX];
    struct tm first_tm;
    memset(&first_tm, 0, sizeof(first_tm));
    first_title[0] = 0;
    body[0] = 0;

    for (int i = 0; i < app->todos.count; i++) {
        Todo* t = &app->todos.items[i];
        /* 已软删除的别提醒 —— 老实现漏了这个判断，回收站里的待办到点照样弹 */
        if (!t->reminder[0] || t->completed || t->deleted) continue;
        struct tm tm_rem;
        if (!parse_date_from_iso(t->reminder, &tm_rem)) continue;
        if (now < mktime(&tm_rem)) continue;      /* 还没到点 */

        total++;
        if (total == 1) {
            first_tm = tm_rem;
            utf8_to_wchar(t->title, first_title, TODO_TITLE_MAX);
        }
        if (total > REMINDER_SHOW_MAX) continue;

        /* 一行：「10-25 09:00  写周报」 */
        WCHAR wt[TODO_TITLE_MAX];
        WCHAR line[320];
        utf8_to_wchar(t->title, wt, TODO_TITLE_MAX);
        _snwprintf(line, 320, L"%02d-%02d %02d:%02d  %ls",
                   tm_rem.tm_mon + 1, tm_rem.tm_mday,
                   tm_rem.tm_hour, tm_rem.tm_min, wt);
        line[319] = 0;
        if (body[0]) wcscat(body, L"\n");
        if (wcslen(body) + wcslen(line) + 2 >= sizeof(body) / sizeof(WCHAR)) break;
        wcscat(body, line);
    }
    if (total == 0) return;

    WCHAR full[1600];
    full[0] = 0;
    if (total == 1) {
        /* 单条：正文给标题 + 原定时间，比列表更好读 */
        _snwprintf(full, 1600, L"%ls\n原定 %02d-%02d %02d:%02d",
                   first_title, first_tm.tm_mon + 1, first_tm.tm_mday,
                   first_tm.tm_hour, first_tm.tm_min);
    } else {
        WCHAR head[160];
        if (at_boot)
            _snwprintf(head, 160, L"程序没开的时候有 %d 条提醒到时间了：", total);
        else
            _snwprintf(head, 160, L"%d 条提醒到时间了：", total);
        head[159] = 0;
        _snwprintf(full, 1600, L"%ls\n%ls", head, body);
        if (total > REMINDER_SHOW_MAX) {
            WCHAR more[64];
            _snwprintf(more, 64, L"\n…等 %d 条", total);
            more[63] = 0;
            if (wcslen(full) + wcslen(more) + 2 < 1600) wcscat(full, more);
        }
    }
    full[1599] = 0;

    Ui_MsgBoxFront(hwnd, at_boot ? L"错过的提醒" : L"提醒", full, MSG_INFO);

    /* 第二遍清空：用第一遍的快照 now，条件完全一致，
       不会把「弹框期间刚到点、没列进框里」的那条误清掉 */
    for (int i = 0; i < app->todos.count; i++) {
        Todo* t = &app->todos.items[i];
        if (!t->reminder[0] || t->completed || t->deleted) continue;
        struct tm tm_rem;
        if (!parse_date_from_iso(t->reminder, &tm_rem)) continue;
        if (now >= mktime(&tm_rem)) t->reminder[0] = '\0';
    }
    SaveTodos(app);
    RefreshTodoTab(app);
}

static void ShowTrayMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    /* 勾选状态就是「注册表里确实是本 exe + 路径没变」——用 Startup_IsEnabled 而不是
       光看 config，否则 exe 挪过位置后还显示成已开启，实际开机不会起来 */
    AppendMenuW(hMenu, MF_STRING | (Startup_IsEnabled() ? MF_CHECKED : MF_UNCHECKED),
                ID_TRAY_AUTOSTART, L"开机自启（静默进托盘）");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

static void BackupData(HWND hwnd, AppState* app) {
    OPENFILENAMEW ofn = {0};
    WCHAR path[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    char pathA[MAX_PATH];
    wchar_to_utf8(path, pathA, MAX_PATH);
    cJSON* root = cJSON_CreateObject();
    cJSON* todos_j = cJSON_CreateArray();
    cJSON* accts_j = cJSON_CreateArray();

    for (int i = 0; i < app->todos.count; i++) {
        cJSON* j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "id", app->todos.items[i].id);
        cJSON_AddStringToObject(j, "title", app->todos.items[i].title);
        cJSON_AddStringToObject(j, "content", app->todos.items[i].content);
        cJSON_AddBoolToObject(j, "completed", app->todos.items[i].completed);
        cJSON_AddStringToObject(j, "created_at", app->todos.items[i].created_at);
        cJSON_AddStringToObject(j, "reminder", app->todos.items[i].reminder);
        cJSON_AddItemToArray(todos_j, j);
    }
    for (int i = 0; i < app->accounts.count; i++) {
        cJSON* j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "id", app->accounts.items[i].id);
        cJSON_AddStringToObject(j, "record_type", app->accounts.items[i].record_type);
        cJSON_AddNumberToObject(j, "amount", app->accounts.items[i].amount);
        cJSON_AddStringToObject(j, "category", app->accounts.items[i].category);
        cJSON_AddStringToObject(j, "date", app->accounts.items[i].date);
        cJSON_AddStringToObject(j, "note", app->accounts.items[i].note);
        cJSON_AddStringToObject(j, "created_at", app->accounts.items[i].created_at);
        cJSON_AddItemToArray(accts_j, j);
    }
    cJSON_AddItemToObject(root, "todos", todos_j);
    cJSON_AddItemToObject(root, "accounts", accts_j);
    char* str = cJSON_Print(root);
    FILE* f = fopen(pathA, "wb");
    if (f) { fwrite(str, 1, strlen(str), f); fclose(f); }
    free(str);
    cJSON_Delete(root);
}

static void ImportData(HWND hwnd, AppState* app) {
    OPENFILENAMEW ofn = {0};
    WCHAR path[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    char pathA[MAX_PATH];
    wchar_to_utf8(path, pathA, MAX_PATH);
    long size;
    char* buf = NULL;
    FILE* f = fopen(pathA, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char*)malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) { Ui_MsgBox(hwnd, L"错误", L"无效的备份文件。", MSG_ERROR); return; }

    int import_todos = 0, import_accts = 0;
    cJSON* todos_j = cJSON_GetObjectItem(root, "todos");
    cJSON* accts_j = cJSON_GetObjectItem(root, "accounts");
    if (todos_j && cJSON_IsArray(todos_j)) import_todos = 1;
    if (accts_j && cJSON_IsArray(accts_j)) import_accts = 1;
    if (!import_todos && !import_accts) {
        cJSON_Delete(root);
        Ui_MsgBox(hwnd, L"提示", L"备份文件中没有可导入的数据。", MSG_INFO);
        return;
    }

    WCHAR msg[256];
    /* %ls 而不是 %s：宽字符格式串里的 %s 在 msvcrt 版运行时会被当成窄字符串 */
    swprintf(msg, 256, L"备份文件包含:\n- 待办事项: %ls\n- 记账记录: %ls\n\n是否导入？",
        import_todos ? L"是" : L"否", import_accts ? L"是" : L"否");
    if (!Ui_MsgBox(hwnd, L"导入数据", msg, MSG_YESNO)) {
        cJSON_Delete(root);
        return;
    }

    if (import_todos) {
        int n = cJSON_GetArraySize(todos_j);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(todos_j, i);
            Todo t;
            cJSON* val;
            todo_init(&t);
            val = cJSON_GetObjectItem(item, "id");
            if (val) t.id = app->next_todo_id++;
            val = cJSON_GetObjectItem(item, "title");
            if (val && val->valuestring) strncpy(t.title, val->valuestring, TODO_TITLE_MAX - 1);
            val = cJSON_GetObjectItem(item, "content");
            if (val && val->valuestring) strncpy(t.content, val->valuestring, TODO_CONTENT_MAX - 1);
            val = cJSON_GetObjectItem(item, "completed");
            if (val) t.completed = val->valueint;
            val = cJSON_GetObjectItem(item, "created_at");
            if (val && val->valuestring) strncpy(t.created_at, val->valuestring, TODO_DATE_STR_LEN - 1);
            val = cJSON_GetObjectItem(item, "reminder");
            if (val && val->valuestring) strncpy(t.reminder, val->valuestring, TODO_DATE_STR_LEN - 1);
            todo_list_add(&app->todos, &t);
        }
        SaveTodos(app);
    }
    if (import_accts) {
        int n = cJSON_GetArraySize(accts_j);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(accts_j, i);
            AccountRecord r;
            acct_init(&r);
            cJSON* val;
            val = cJSON_GetObjectItem(item, "id");
            if (val) r.id = app->next_acct_id++;
            val = cJSON_GetObjectItem(item, "record_type");
            if (val && val->valuestring) strncpy(r.record_type, val->valuestring, ACCT_TYPE_LEN - 1);
            val = cJSON_GetObjectItem(item, "amount");
            if (val) r.amount = val->valuedouble;
            val = cJSON_GetObjectItem(item, "category");
            if (val && val->valuestring) strncpy(r.category, val->valuestring, ACCT_CAT_MAX - 1);
            val = cJSON_GetObjectItem(item, "date");
            if (val && val->valuestring) {
                strncpy(r.date, val->valuestring, ACCT_DATE_LEN - 1);
                normalize_date(r.date);
            }
            val = cJSON_GetObjectItem(item, "note");
            if (val && val->valuestring) strncpy(r.note, val->valuestring, ACCT_NOTE_MAX - 1);
            val = cJSON_GetObjectItem(item, "created_at");
            if (val && val->valuestring) strncpy(r.created_at, val->valuestring, ACCT_TIME_LEN - 1);
            acct_list_add(&app->accounts, &r);
        }
        SaveAccounts(app);
    }
    cJSON_Delete(root);
    RefreshTodoTab(app);
    RefreshAccountTab(app);
}

/* ---------------- 导出 CSV ---------------- */

static void ExportCsvData(HWND hwnd, AppState* app) {
    OPENFILENAMEW ofn = {0};
    WCHAR path[MAX_PATH] = {0};
    char  name[64], today[16], ymd[16];

    get_today(today, sizeof(today));           /* YYYY/MM/DD */
    int k = 0;
    for (int i = 0; today[i] && k < 15; i++)
        if (today[i] != '/') ymd[k++] = today[i];
    ymd[k] = 0;
    snprintf(name, sizeof(name), "accounts-%s.csv", ymd);
    utf8_to_wchar(name, path, MAX_PATH);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"CSV Files\0*.csv\0All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags       = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;

    char pathA[MAX_PATH];
    wchar_to_utf8(path, pathA, MAX_PATH);
    int n = storage_export_accounts_csv(pathA, &app->accounts, 0);

    WCHAR msg[128];
    if (n < 0) {
        Ui_MsgBox(hwnd, L"导出失败", L"写不进这个位置，换个路径再试一次。", MSG_ERROR);
        return;
    }
    swprintf(msg, 128, L"已导出 %d 条记录。\n用 Excel 直接打开即可，中文不会乱码。", n);
    Ui_MsgBox(hwnd, L"导出完成", msg, MSG_INFO);
}

/* ---------------- 最近删除 ---------------- */

#define TRASH_TODO_BIT 0x40000000u
#define TRASH_ID_MASK  0x00FFFFFFu

/* ISO 时间戳（2026-10-23T18:20:00）→ 显示用；mode 0 = 只要日期(2026/10/23)，1 = 日期+时分 */
static void fmt_stamp_utf8(const char* iso, char* out, int cap, int with_time) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!iso || !iso[0]) return;
    char tmp[64];
    strncpy(tmp, iso, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char* sep = strchr(tmp, 'T');
    if (!sep) sep = strchr(tmp, ' ');
    if (with_time) {
        if (sep) {
            *sep = ' ';
            char* p = sep + 1;
            if (strlen(p) > 5) p[5] = 0;
        }
    } else if (sep) {
        *sep = 0;
    }
    for (char* p = tmp; *p; p++) if (*p == '-') *p = '/';
    snprintf(out, (size_t)cap, "%s", tmp);
}

static void trash_fill(HWND h, AppState* app) {
    HWND lv = GetDlgItem(h, ID_TRASH_LIST);
    if (!lv) return;
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);

    WCHAR wa[512], wb[512], wc[512];
    char  buf[512];
    int   row = 0;

    for (int i = 0; i < app->accounts.count; i++) {
        AccountRecord* r = &app->accounts.items[i];
        if (!r->deleted) continue;

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = row++;
        lvi.lParam = (LPARAM)(((DWORD)r->id) & TRASH_ID_MASK);
        lvi.pszText = L"记账";
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        utf8_to_wchar(r->date, wa, 512);
        lvi.iSubItem = 1; lvi.mask = LVIF_TEXT; lvi.pszText = wa;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        utf8_to_wchar(r->category, wa, 512);
        lvi.iSubItem = 2; lvi.pszText = wa;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        {
            char mv[64];
            fmt_money_utf8(mv, sizeof(mv), r->amount, 2);
            snprintf(buf, sizeof(buf), "%s%s", acct_is_income(r) ? "+" : "-", mv);
        }
        utf8_to_wchar(buf, wb, 512);
        lvi.iSubItem = 3; lvi.pszText = wb;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        fmt_stamp_utf8(r->deleted_at, buf, sizeof(buf), 1);
        utf8_to_wchar(buf, wc, 512);
        lvi.iSubItem = 4; lvi.pszText = wc;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }

    for (int i = 0; i < app->todos.count; i++) {
        Todo* t = &app->todos.items[i];
        if (!t->deleted) continue;

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = row++;
        lvi.lParam = (LPARAM)(TRASH_TODO_BIT | (((DWORD)t->id) & TRASH_ID_MASK));
        lvi.pszText = L"待办";
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        fmt_stamp_utf8(t->created_at, buf, sizeof(buf), 0);
        utf8_to_wchar(buf, wa, 512);
        lvi.iSubItem = 1; lvi.mask = LVIF_TEXT; lvi.pszText = wa;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        utf8_to_wchar(t->title, wa, 512);
        lvi.iSubItem = 2; lvi.pszText = wa;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 3; lvi.pszText = L"";
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        fmt_stamp_utf8(t->deleted_at, buf, sizeof(buf), 1);
        utf8_to_wchar(buf, wc, 512);
        lvi.iSubItem = 4; lvi.pszText = wc;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }
}

static LPARAM trash_selected(HWND h) {
    HWND lv = GetDlgItem(h, ID_TRASH_LIST);
    if (!lv) return -1;
    int idx = (int)SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (idx < 0) return -1;
    LVITEMW it = {0};
    it.mask = LVIF_PARAM;
    it.iItem = idx;
    if (!SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&it)) return -1;
    return (LPARAM)it.lParam;
}

static int trash_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    AppState* app = dlg_of_app(h);
    int id = LOWORD(wp);
    if (!app) return 1;

    if (id == ID_TRASH_RESTORE) {
        LPARAM sel = trash_selected(h);
        if (sel < 0) return 1;
        if (sel & TRASH_TODO_BIT) todo_list_restore(&app->todos, (int)(sel & TRASH_ID_MASK));
        else                      acct_list_restore(&app->accounts, (int)(sel & TRASH_ID_MASK));
        SaveTodos(app); SaveAccounts(app);
        RefreshTodoTab(app); RefreshAccountTab(app);
        trash_fill(h, app);
        return 1;
    }
    if (id == ID_TRASH_PURGE) {
        LPARAM sel = trash_selected(h);
        if (sel < 0) return 1;
        if (!Ui_MsgBox(h, L"彻底删除", L"这条记录会永久消失，无法恢复。确定吗？", MSG_YESNO))
            return 1;
        if (sel & TRASH_TODO_BIT) todo_list_remove(&app->todos, (int)(sel & TRASH_ID_MASK));
        else                      acct_list_remove(&app->accounts, (int)(sel & TRASH_ID_MASK));
        SaveTodos(app); SaveAccounts(app);
        trash_fill(h, app);
        return 1;
    }
    if (id == ID_TRASH_EMPTY) {
        if (!Ui_MsgBox(h, L"清空回收站", L"最近删除里的全部记录都会永久消失。确定吗？", MSG_YESNO))
            return 1;
        for (int i = app->accounts.count - 1; i >= 0; i--)
            if (app->accounts.items[i].deleted)
                acct_list_remove(&app->accounts, app->accounts.items[i].id);
        for (int i = app->todos.count - 1; i >= 0; i--)
            if (app->todos.items[i].deleted)
                todo_list_remove(&app->todos, app->todos.items[i].id);
        SaveTodos(app); SaveAccounts(app);
        trash_fill(h, app);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

static void ShowTrashDlg(HWND owner, AppState* app) {
    enum { W = 720, LIST_Y = 56, LIST_H = 300 };
    int H = LIST_Y + LIST_H + 20 + DLG_BTN_H + 20;
    HWND d = Ui_DlgBegin(owner, L"最近删除", W, H, trash_proc);
    if (!d) return;
    SetPropW(d, L"ctx2", (HANDLE)app);

    Ui_DlgLabel(d, L"删除的记录会保留 30 天，期间可以随时恢复。", 20, 26, 480, 18);

    HWND lv = Ui_DlgList(d, ID_TRASH_LIST, 20, LIST_Y, W - 40, LIST_H);
    {
        static const WCHAR* nm[5] = { L"类型", L"日期", L"内容", L"金额", L"删除时间" };
        static const int ws[5] = { 66, 104, 250, 92, 140 };
        static const int rt[5] = { 0, 0, 0, 1, 0 };
        lv_cols(lv, nm, ws, rt, 5);
    }

    Ui_DlgButton(d, ID_TRASH_RESTORE, L"恢复", 20, DLG_BTN_Y(H), 96, DLG_BTN_H, 1);
    Ui_DlgButton(d, ID_TRASH_PURGE,   L"彻底删除", 128, DLG_BTN_Y(H), 96, DLG_BTN_H, 0);
    Ui_DlgButton(d, ID_TRASH_EMPTY,   L"清空", 236, DLG_BTN_Y(H), 80, DLG_BTN_H, 0);
    Ui_DlgButton(d, IDCANCEL, L"关闭", DLG_OK_X(W), DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 0);

    trash_fill(d, app);
    Ui_DlgRun(d);
}

/* ---------------- 预算设置 ---------------- */

/* 本月某分类的支出（跳过收入与已删除） */
static double cat_month_expense(AppState* app, const char* cat, const char* month) {
    double sum = 0;
    for (int i = 0; i < app->accounts.count; i++) {
        AccountRecord* r = &app->accounts.items[i];
        if (r->deleted || acct_is_income(r)) continue;
        if (strcmp(r->category, cat) != 0) continue;
        char im[16];
        get_month_from_date(r->date, im, sizeof(im));
        if (strcmp(im, month) == 0) sum += r->amount;
    }
    return sum;
}

/* 预算表要列多少行：8 个标准分类 + 已设预算的非标准分类（去重） */
static int budget_row_count(AppState* app) {
    int n = ACCT_STD_CAT_N;
    for (int i = 0; i < app->budgets.count; i++) {
        const char* c = app->budgets.items[i].category;
        int dup = 0;
        for (int j = 0; j < ACCT_STD_CAT_N; j++)
            if (strcmp(kStdCategories[j], c) == 0) { dup = 1; break; }
        if (!dup) n++;
    }
    return n;
}

static void budget_fill(HWND h, AppState* app) {
    HWND lv = GetDlgItem(h, ID_BUDGET_LIST);
    if (!lv) return;
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);

    char month[16];
    if (app->acct_month[0]) {
        strncpy(month, app->acct_month, sizeof(month) - 1);
        month[sizeof(month) - 1] = 0;
    } else {
        char today[DATE_STR_LEN];
        get_today(today, sizeof(today));
        get_month_from_date(today, month, sizeof(month));
    }

    WCHAR w[512];
    int row = 0;
    /* 标准 8 类 + 其它已设预算的分类（标准类不重复列，否则同一个分类出现两行） */
    for (int i = 0; i < ACCT_STD_CAT_N + app->budgets.count; i++) {
        const char* cat = NULL;
        int std_n = ACCT_STD_CAT_N;
        if (i < std_n) {
            cat = kStdCategories[i];
        } else {
            cat = app->budgets.items[i - std_n].category;
            int dup = 0;
            for (int j = 0; j < ACCT_STD_CAT_N; j++)
                if (strcmp(kStdCategories[j], cat) == 0) { dup = 1; break; }
            if (dup) continue;
        }
        if (!cat || !cat[0]) continue;

        double bud = budget_list_get(&app->budgets, cat);
        double used = cat_month_expense(app, cat, month);

        /* 分类列画成彩色 chip（和记账列表同一套色板）。
         * lParam：25–28 位分类色号、30 位超支标记（Ui_ListCustomDraw 靠它上色）。 */
        int ci = Ui_CatPresetIndex(cat);
        if (ci < 0) ci = 14;                       /* 自定义分类统一用石板色 */
        int over = (bud > 0 && used > bud + 0.005) ? 1 : 0;

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = row++;
        lvi.lParam = (LPARAM)((((DWORD)ci & 0xF) << 25) | ((DWORD)over << 30));
        utf8_to_wchar(cat, w, 512);
        lvi.pszText = w;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        char buf[128];
        if (bud > 0) fmt_money_utf8(buf, sizeof(buf), bud, 0);
        else         snprintf(buf, sizeof(buf), "—");
        utf8_to_wchar(buf, w, 512);
        lvi.iSubItem = 1; lvi.mask = LVIF_TEXT; lvi.pszText = w;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        fmt_money_utf8(buf, sizeof(buf), used, 2);
        utf8_to_wchar(buf, w, 512);
        lvi.iSubItem = 2; lvi.pszText = w;
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);

        lvi.iSubItem = 3;
        if (bud <= 0) {
            lvi.pszText = L"未设预算";
        } else {
            double left = bud - used;
            char lb[64];
            if (left < -0.005) {
                fmt_money_utf8(lb, sizeof(lb), -left, 0);
                snprintf(buf, sizeof(buf), "超支 %s", lb);
            } else {
                fmt_money_utf8(lb, sizeof(lb), left, 0);
                snprintf(buf, sizeof(buf), "剩余 %s（%.0f%%）", lb, left * 100.0 / bud);
            }
            utf8_to_wchar(buf, w, 512);
            lvi.pszText = w;
        }
        SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&lvi);
    }
}

/* 底部提示行：跟着「分类」框实时显示本月已花与已设预算 */
static void budget_hint(HWND h, AppState* app) {
    HWND hCat  = GetDlgItem(h, ID_BUDGET_CAT);
    HWND hHint = GetDlgItem(h, ID_BUDGET_HINT);
    if (!hCat || !hHint) return;

    char  cat[64];
    if (!Ui_ComboGetText(hCat, cat, 64) || !cat[0]) {
        SetWindowTextW(hHint, L"选一个分类，填 0 或留空表示取消该分类的预算");
        return;
    }

    char month[16];
    if (app->acct_month[0]) {
        strncpy(month, app->acct_month, sizeof(month) - 1);
        month[sizeof(month) - 1] = 0;
    } else {
        char today[DATE_STR_LEN];
        get_today(today, sizeof(today));
        get_month_from_date(today, month, sizeof(month));
    }

    double used = cat_month_expense(app, cat, month);
    double bud  = budget_list_get(&app->budgets, cat);
    char u[48], bd[48], out[192];
    fmt_money_utf8(u, sizeof(u), used, 2);
    if (bud > 0) {
        fmt_money_utf8(bd, sizeof(bd), bud, 0);
        snprintf(out, sizeof(out), "本月已花 %s · 当前预算 %s", u, bd);
    } else {
        snprintf(out, sizeof(out), "本月已花 %s · 该分类尚未设预算", u);
    }
    WCHAR wo[192];
    utf8_to_wchar(out, wo, 192);
    SetWindowTextW(hHint, wo);
}

static int budget_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    AppState* app = dlg_of_app(h);
    int id = LOWORD(wp);
    if (!app) return 1;

    /* 选了分类就把现有预算填进金额框，省得自己回想上次设了多少 */
    if (id == ID_BUDGET_CAT && HIWORD(wp) == CBN_SELCHANGE) {
        char  cat[64];
        Ui_ComboGetText(GetDlgItem(h, ID_BUDGET_CAT), cat, 64);
        if (cat[0]) {
            double bud = budget_list_get(&app->budgets, cat);
            char nb[32];
            if (bud > 0) snprintf(nb, sizeof(nb), "%.0f", bud);
            else         nb[0] = 0;
            WCHAR wnb[32];
            utf8_to_wchar(nb, wnb, 32);
            SetWindowTextW(GetDlgItem(h, ID_BUDGET_EDIT), wnb);
        }
        budget_hint(h, app);
        return 1;
    }
    if (id == ID_BUDGET_EDIT && HIWORD(wp) == EN_CHANGE) {
        budget_hint(h, app);
        return 1;
    }

    if (id == IDOK) {
        WCHAR wamt[64];
        char cat[64], amt[64];
        GetWindowTextW(GetDlgItem(h, ID_BUDGET_EDIT), wamt, 64);
        wchar_to_utf8(wamt, amt, 64);
        if (!Ui_ComboGetText(GetDlgItem(h, ID_BUDGET_CAT), cat, 64)) {
            Ui_MsgBox(h, L"提示", L"请先选择分类。", MSG_WARN);
            return 1;
        }
        double v = atof(amt);
        if (v < 0) v = 0;
        budget_list_set(&app->budgets, cat, v);
        SaveBudgets(app);
        budget_fill(h, app);
        budget_hint(h, app);
        Ui_MsgBox(h, L"已保存",
                  v > 0 ? L"预算已保存。" : L"该分类的预算已取消。", MSG_INFO);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

static void ShowBudgetDlg(HWND owner, AppState* app) {
    enum { W = 640, LIST_Y = 56 };
    int rows = app ? budget_row_count(app) : ACCT_STD_CAT_N;
    if (rows < 4) rows = 4;
    if (rows > 12) rows = 12;
    int LIST_H = rows * 30 + 18;
    int H = LIST_Y + LIST_H + 20 + 34 + 26 + 30 + DLG_BTN_H + 20;

    HWND d = Ui_DlgBegin(owner, L"预算设置", W, H, budget_proc);
    if (!d) return;
    SetPropW(d, L"ctx2", (HANDLE)app);

    Ui_DlgLabel(d, L"按月设定每个分类的支出上限，统计报表里超支会标红。", 20, 26, 520, 18);

    HWND lv = Ui_DlgList(d, ID_BUDGET_LIST, 20, LIST_Y, W - 40, LIST_H);
    /* 「分类」是第 0 列，画成彩色 chip；超支的行整行标红 */
    SetPropW(lv, L"chipCol", (HANDLE)(INT_PTR)1);
    {
        static const WCHAR* nm[4] = { L"分类", L"月预算", L"本月已花", L"状态" };
        static const int ws[4] = { 130, 110, 120, 210 };
        static const int rt[4] = { 0, 1, 1, 0 };
        lv_cols(lv, nm, ws, rt, 4);
    }

    int fy = LIST_Y + LIST_H + 16;
    Ui_DlgLabel(d, L"分类", 20, fy + 8, 40, 18);
    {
        HINSTANCE dInst = (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE);
        HWND cCat = Ui_ComboCreate(d, ID_BUDGET_CAT, dInst, kStdCategories, ACCT_STD_CAT_N);
        if (cCat) {
            SetWindowPos(cCat, NULL, Ui_S(66), Ui_S(fy), Ui_S(150), Ui_S(34), SWP_NOZORDER);
            Ui_ComboSetColorize(cCat, 1);
            /* 8 类之外的历史自定义分类也补进下拉框，否则老数据没法再设预算 */
            if (app) {
                for (int i = 0; i < app->budgets.count; i++)
                    Ui_ComboAddItem(cCat, app->budgets.items[i].category);
            }
            Ui_ComboSetSel(cCat, 0);
        }
    }

    Ui_DlgLabel(d, L"月预算", 236, fy + 8, 56, 18);
    HWND eAmt = Ui_DlgEdit(d, ID_BUDGET_EDIT, 296, fy, 120, 34, 0);
    Ui_AmountEditGuard(eAmt);

    HWND hHint = Ui_DlgLabel(d, L"点分类框选一个分类，填 0 或留空表示取消该分类的预算",
                             20, fy + 42, W - 40, 18);
    SetWindowLongPtrW(hHint, GWLP_ID, ID_BUDGET_HINT);

    Ui_DlgButton(d, IDOK, L"保存预算", DLG_OK_X(W), DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 1);
    Ui_DlgButton(d, IDCANCEL, L"关闭", DLG_CANCEL_X(W), DLG_BTN_Y(H), DLG_BTN_W_NO, DLG_BTN_H, 0);

    budget_fill(d, app);
    Ui_DlgRun(d);
}

static void AddTodoItem(AppState* app) {
    WCHAR wtitle[TODO_TITLE_MAX], wcontent[TODO_CONTENT_MAX];
    char title[TODO_TITLE_MAX], content[TODO_CONTENT_MAX];
    GetWindowTextW(app->hTodoTitle, wtitle, TODO_TITLE_MAX);
    GetWindowTextW(app->hTodoContent, wcontent, TODO_CONTENT_MAX);
    wchar_to_utf8(wtitle, title, TODO_TITLE_MAX);
    wchar_to_utf8(wcontent, content, TODO_CONTENT_MAX);
    if (!title[0]) {
        Ui_MsgBox(GetAncestor(app->hTodoPanel, GA_ROOT), L"提示", L"请先填写标题。", MSG_WARN);
        return;
    }
    Todo t;
    todo_init(&t);
    t.id = app->next_todo_id++;
    strncpy(t.title, title, TODO_TITLE_MAX - 1);
    strncpy(t.content, content, TODO_CONTENT_MAX - 1);
    t.priority = app->todo_prio;              /* 面板上选的那档，默认「不重要」 */
    get_now_str(t.created_at, sizeof(t.created_at));
    todo_list_add(&app->todos, &t);
    SaveTodos(app);
    SetWindowTextW(app->hTodoTitle, L"");
    SetWindowTextW(app->hTodoContent, L"");
    RefreshTodoTab(app);
}

static void DeleteTodoItem(AppState* app, int id) {
    if (WriteBlocked(app)) return;
    WCHAR msg[128];
    swprintf(msg, 128, L"删除待办 #%d？\n删掉的会留在「最近删除」里 30 天，可以随时找回。", id);
    if (!Ui_MsgBox(GetAncestor(app->hTodoPanel, GA_ROOT), L"确认删除", msg, MSG_YESNO))
        return;
    todo_list_soft_delete(&app->todos, id);
    SaveTodos(app);
    RefreshTodoTab(app);
}

/* 勾选/取消勾选。待办不再有「重复」，勾完就是完成，不会自动生成下一期。 */
static void ToggleTodoItem(AppState* app, int id) {
    if (WriteBlocked(app)) return;
    Todo* t = todo_list_find(&app->todos, id);
    if (!t) return;
    t->completed = !t->completed;

    SaveTodos(app);
    RefreshTodoTab(app);
}

static void EditTodoItem(HWND hwnd, AppState* app, int id) {
    Todo* t = todo_list_find(&app->todos, id);
    if (!t) return;
    ShowEditTodoDlg(hwnd, t);
    SaveTodos(app);
    RefreshTodoTab(app);
}

static void SetReminder(HWND hwnd, AppState* app, int id) {
    Todo* t = todo_list_find(&app->todos, id);
    if (!t) return;
    ShowReminderDlg(hwnd, t);
    SaveTodos(app);
    RefreshTodoTab(app);
}

static void AddAccountItem(AppState* app) {
    if (WriteBlocked(app)) return;
    WCHAR wbuf[256];
    char buf[256], cat[64], date[16], note[256];
    double amount;

    GetWindowTextW(app->hAcctAmount, wbuf, 256);
    wchar_to_utf8(wbuf, buf, 256);
    amount = atof(buf);
    if (amount <= 0) {
        Ui_MsgBox(GetAncestor(app->hAcctPanel, GA_ROOT), L"提示", L"请输入有效的金额。", MSG_WARN);
        return;
    }

    if (!Ui_ComboGetText(app->hAcctCatCombo, cat, 64)) {
        Ui_MsgBox(GetAncestor(app->hAcctPanel, GA_ROOT), L"提示", L"请选择分类。", MSG_WARN);
        return;
    }

    GetWindowTextW(app->hAcctDate, wbuf, 64);
    wchar_to_utf8(wbuf, date, 16);
    normalize_date(date);
    if (!is_valid_date(date)) {
        Ui_MsgBox(GetAncestor(app->hAcctPanel, GA_ROOT), L"日期无效",
                  L"日期格式不正确，请按 YYYY-MM-DD 填写。", MSG_ERROR);
        SetFocus(app->hAcctDate);
        return;
    }

    GetWindowTextW(app->hAcctNote, wbuf, 256);
    wchar_to_utf8(wbuf, note, 256);

    AccountRecord r;
    acct_init(&r);
    r.id = app->next_acct_id++;
    strcpy(r.record_type, app->acct_type ? "income" : "expense");
    r.amount = amount;
    strncpy(r.category, cat, ACCT_CAT_MAX - 1);
    strncpy(r.date, date, ACCT_DATE_LEN - 1);
    normalize_date(r.date);
    strncpy(r.note, note, ACCT_NOTE_MAX - 1);
    acct_list_add(&app->accounts, &r);
    SaveAccounts(app);

    SetWindowTextW(app->hAcctAmount, L"");
    SetWindowTextW(app->hAcctNote, L"");
    char today[16];
    get_today(today, sizeof(today));
    WCHAR wtoday[32];
    utf8_to_wchar(today, wtoday, 32);
    SetWindowTextW(app->hAcctDate, wtoday);

    RefreshAccountTab(app);
}

static void DeleteAccountItem(AppState* app, int id) {
    if (WriteBlocked(app)) return;
    WCHAR msg[128];
    swprintf(msg, 128, L"删除记账记录 #%d？\n删掉的会留在「最近删除」里 30 天，可以随时找回。", id);
    if (!Ui_MsgBox(GetAncestor(app->hAcctPanel, GA_ROOT), L"确认删除", msg, MSG_YESNO))
        return;
    acct_list_soft_delete(&app->accounts, id);
    SaveAccounts(app);
    RefreshAccountTab(app);
}

static void EditAccountItem(HWND hwnd, AppState* app, int id) {
    AccountRecord* r = acct_list_find(&app->accounts, id);
    if (!r) return;
    ShowEditAcctDlg(hwnd, r);
    SaveAccounts(app);
    RefreshAccountTab(app);
}

/* 分类统计与月度报表原本是两个独立对话框，但填充逻辑逐行相同
 * （当月分类金额 + 占比 + 总计行），只是后者多了环形图。
 * 已合并为一个「统计报表」，入口只留一个按钮。 */
static void ShowMonthlyReport(HWND hwnd, AppState* app) {
    ShowMonthlyDlg(hwnd, app);
}

static void HandleAccountListDblClick(HWND hwnd, AppState* app) {
    HWND hList = app->hAcctList;
    int idx = (int)SendMessageW(hList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (idx < 0) return;
    LVITEMW lvi;
    lvi.mask = LVIF_PARAM;
    lvi.iItem = idx;
    if (SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi))
        EditAccountItem(hwnd, app, (int)(lvi.lParam & 0xFFFFFF));
}

/* 右键菜单：坐标是面板客户区坐标，先命中象限里的行 */
static void HandleTodoRClick(HWND hwnd, AppState* app, int x, int y) {
    POINT p = { x, y };
    int row = -1;
    int q = Ui_QuadHit(app, p, &row);
    if (q < 0 || row < 0 || row >= app->quad_n[q]) return;

    int id = app->quad_ids[q][row];
    if (!id) return;
    app->quad_sel_id = id;
    InvalidateRect(app->hTodoPanel, NULL, FALSE);

    UiMenuItem items[8];
    memset(items, 0, sizeof(items));
    int n = 0;
    Todo* t = todo_list_find(&app->todos, id);
    if (t) {
        items[n].id = 4001;
        items[n].label = t->completed ? L"标记为未完成" : L"标记为完成";
        n++;
    }
    items[n].id = 4002; items[n].label = L"编辑";       n++;
    items[n].id = 4003; items[n].label = L"设置提醒";   n++;
    items[n].id = 0;    items[n].label = NULL;         n++;
    /* 改象限：右键直接挪，不用打开对话框 */
    items[n].id = 4010; items[n].label = L"移到 重要且紧急"; n++;
    items[n].id = 4011; items[n].label = L"移到 重要不紧急"; n++;
    items[n].id = 4012; items[n].label = L"移到 紧急不重要"; n++;
    items[n].id = 4013; items[n].label = L"移到 不重要不紧急"; n++;
    items[n].id = 0;    items[n].label = NULL;         n++;
    items[n].id = 4004; items[n].label = L"删除";       n++;

    POINT pt_menu = { x, y };
    ClientToScreen(app->hTodoPanel, &pt_menu);
    int cmd = Ui_PopMenu(hwnd, pt_menu.x, pt_menu.y, items, n);
    switch (cmd) {
        case 4001: ToggleTodoItem(app, id); break;
        case 4002: EditTodoItem(hwnd, app, id); break;
        case 4003: SetReminder(hwnd, app, id); break;
        case 4004: DeleteTodoItem(app, id); break;
        case 4010: case 4011: case 4012: case 4013: {
            if (WriteBlocked(app)) break;
            Todo* td = todo_list_find(&app->todos, id);
            if (!td) break;
            int nq = cmd - 4010;                 /* 0..3 → 象限号 */
            td->priority = TODO_QUAD_PRIO(nq);
            SaveTodos(app);
            RefreshTodoTab(app);
            break;
        }
    }
}

static void HandleAccountListRClick(HWND hwnd, AppState* app, int x, int y) {
    HWND hList = app->hAcctList;
    LVHITTESTINFO ht;
    ht.pt.x = x;
    ht.pt.y = y;
    int idx = (int)SendMessageW(hList, LVM_HITTEST, 0, (LPARAM)&ht);
    if (idx < 0) return;
    LVITEMW lvis = {0};
    lvis.stateMask = LVIS_SELECTED;
    lvis.state = LVIS_SELECTED;
    SendMessageW(hList, LVM_SETITEMSTATE, idx, (LPARAM)&lvis);
    LVITEMW lvi;
    lvi.mask = LVIF_PARAM;
    lvi.iItem = idx;
    if (!SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi)) return;
    int id = (int)(lvi.lParam & 0xFFFFFF);

    UiMenuItem items[3];
    memset(items, 0, sizeof(items));
    items[0].id = 5001; items[0].label = L"编辑";
    items[1].id = 0;    items[1].label = NULL;
    items[2].id = 5002; items[2].label = L"删除";

    POINT pt_menu = {x, y};
    ClientToScreen(hList, &pt_menu);
    int cmd = Ui_PopMenu(hwnd, pt_menu.x, pt_menu.y, items, 3);
    switch (cmd) {
        case 5001: EditAccountItem(hwnd, app, id); break;
        case 5002: DeleteAccountItem(app, id); break;
    }
}

/* ---------------- 自绘对话框 ---------------- */

static Todo*          dlg_of_todo(HWND h) { return (Todo*)GetPropW(h, L"ctx"); }
static AccountRecord* dlg_of_acct(HWND h) { return (AccountRecord*)GetPropW(h, L"ctx"); }
static AppState*      dlg_of_app(HWND h)  { return (AppState*)GetPropW(h, L"ctx2"); }

/* 列表统一加项（必须用 W 版消息：本项目未定义 UNICODE 宏，
 *  ListView_InsertItem 宏会退化成 A 版，中文会变乱码） */
/* 多列列表：names/widths/rights 长度均为 n */
static void lv_cols(HWND lv, const WCHAR* const* names, const int* widths,
                    const int* rights, int n) {
    for (int i = 0; i < n; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
        col.fmt = rights[i] ? LVCFMT_RIGHT : LVCFMT_LEFT;
        col.cx = Ui_S(widths[i]);
        col.pszText = (WCHAR*)names[i];
        SendMessageW(lv, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
    }
}

/* 三列行：分类 / 金额 / 占比 */
static void lv_add_row4(HWND lv, const WCHAR* c0, const WCHAR* c1, const WCHAR* c2,
                        const WCHAR* c3, DWORD lparam) {
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.lParam = (LPARAM)lparam;
    it.iItem = (int)SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0);
    it.iSubItem = 0;
    it.pszText = (WCHAR*)c0;
    SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);

    it.mask = LVIF_TEXT;
    it.iSubItem = 1;
    it.pszText = (WCHAR*)c1;
    SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)it.iItem, (LPARAM)&it);
    it.iSubItem = 2;
    it.pszText = (WCHAR*)c2;
    SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)it.iItem, (LPARAM)&it);
    if (c3) {
        it.iSubItem = 3;
        it.pszText = (WCHAR*)c3;
        SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)it.iItem, (LPARAM)&it);
    }
}

static void lv_add_row(HWND lv, const WCHAR* c0, const WCHAR* c1, const WCHAR* c2,
                       int ci) {
    lv_add_row4(lv, c0, c1, c2, NULL, ((DWORD)ci & 0xF) << 25);
}

/* ---- 1. 编辑待办 ---- */
/* 四个优先级按钮：选中的那个铺语义色浅底 + 语义色文字（Ui_DrawOwnerdrawButton 认 "primary"/"prio"） */
static void prio_refresh(HWND d, const Todo* t) {
    static const int ids[4]  = { ID_TODO_PRIO_BOTH, ID_TODO_PRIO_IMPORTANT,
                                 ID_TODO_PRIO_URGENT, ID_TODO_PRIO_NONE };
    static const int pri[4]  = { TODO_PRIO_BOTH, TODO_PRIO_IMPORTANT,
                                 TODO_PRIO_URGENT, TODO_PRIO_NONE };
    for (int i = 0; i < 4; i++) {
        HWND b = GetDlgItem(d, ids[i]);
        if (!b) continue;
        if (t->priority == pri[i]) SetPropW(b, L"primary", (HANDLE)1);
        else                       RemovePropW(b, L"primary");
        /* prio 属性 = 象限号 + 1（0 留给「不是优先级按钮」） */
        SetPropW(b, L"prio", (HANDLE)(INT_PTR)(TODO_PRIO_QUAD(pri[i]) + 1));
        InvalidateRect(b, NULL, TRUE);
    }
}

static int edit_todo_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    int id = LOWORD(wp);
    Todo* t = dlg_of_todo(h);
    if (!t) return 0;
    if (id == IDOK) {
        WCHAR wbuf[512];
        GetWindowTextW(GetDlgItem(h, 101), wbuf, 512);
        char title[TODO_TITLE_MAX];
        wchar_to_utf8(wbuf, title, TODO_TITLE_MAX);
        if (!title[0]) {
            Ui_MsgBox(h, L"提示", L"标题不能为空。", MSG_WARN);
            SetFocus(GetDlgItem(h, 101));
            return 1;
        }
        strncpy(t->title, title, TODO_TITLE_MAX - 1);
        t->title[TODO_TITLE_MAX - 1] = 0;
        GetWindowTextW(GetDlgItem(h, 103), wbuf, 512);
        wchar_to_utf8(wbuf, t->content, TODO_CONTENT_MAX);
        Ui_DlgFinish(h, 1);
        return 1;
    }
    if (id == ID_TODO_PRIO_BOTH || id == ID_TODO_PRIO_IMPORTANT ||
        id == ID_TODO_PRIO_URGENT || id == ID_TODO_PRIO_NONE) {
        t->priority = (id == ID_TODO_PRIO_BOTH)      ? TODO_PRIO_BOTH
                    : (id == ID_TODO_PRIO_IMPORTANT) ? TODO_PRIO_IMPORTANT
                    : (id == ID_TODO_PRIO_URGENT)    ? TODO_PRIO_URGENT
                                                     : TODO_PRIO_NONE;
        prio_refresh(h, t);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

void ShowEditTodoDlg(HWND owner, Todo* t) {
    enum { W = 470, H = 420 };
    HWND d = Ui_DlgBegin(owner, L"编辑待办事项", W, H, edit_todo_proc);
    if (!d) return;
    SetPropW(d, L"ctx", (HANDLE)t);

    Ui_DlgLabel(d, L"标题", 20, 52, 60, 18);
    HWND e1 = Ui_DlgEdit(d, 101, 20, 76, 430, 34, 0);
    Ui_DlgLabel(d, L"内容", 20, 118, 60, 18);
    HWND e2 = Ui_DlgEdit(d, 103, 20, 142, 430, 104, 1);
    SendMessageW(e1, EM_SETLIMITTEXT, UI_LIMIT_CHARS(TODO_TITLE_MAX), 0);
    SendMessageW(e2, EM_SETLIMITTEXT, UI_LIMIT_CHARS(TODO_CONTENT_MAX), 0);

    WCHAR wbuf[512];
    utf8_to_wchar(t->title, wbuf, 512);
    SetWindowTextW(e1, wbuf);
    utf8_to_wchar(t->content, wbuf, 512);
    SetWindowTextW(e2, wbuf);

    /* 优先级：四象限选一，卡片底色就是这一档的颜色 */
    Ui_DlgLabel(d, L"优先级", 20, 258, 60, 18);
    Ui_DlgButton(d, ID_TODO_PRIO_BOTH,      L"重要且紧急",  20, 280, 92, 34, 0);
    Ui_DlgButton(d, ID_TODO_PRIO_IMPORTANT, L"重要不紧急", 120, 280, 92, 34, 0);
    Ui_DlgButton(d, ID_TODO_PRIO_URGENT,    L"紧急不重要", 220, 280, 92, 34, 0);
    Ui_DlgButton(d, ID_TODO_PRIO_NONE,      L"不重要不紧急", 320, 280, 108, 34, 0);
    prio_refresh(d, t);

    Ui_DlgButton(d, IDCANCEL, L"取消", DLG_CANCEL_X(W), DLG_BTN_Y(H), DLG_BTN_W_NO, DLG_BTN_H, 0);
    Ui_DlgButton(d, IDOK,     L"保存", DLG_OK_X(W),     DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 1);
    Ui_DlgRun(d);
}

/* ---- 2. 编辑记账记录 ---- */
static int edit_acct_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    int id = LOWORD(wp);
    AccountRecord* rec = dlg_of_acct(h);
    if (!rec) return 0;
    if (id == IDOK) {
        WCHAR wbuf[256];
        GetWindowTextW(GetDlgItem(h, 101), wbuf, 256);
        double amount = wcstod(wbuf, NULL);
        if (!(amount > 0)) {
            Ui_MsgBox(h, L"金额无效", L"金额必须是大于 0 的数字。", MSG_WARN);
            SetFocus(GetDlgItem(h, 101));
            SendMessageW(GetDlgItem(h, 101), EM_SETSEL, 0, -1);
            return 1;
        }
        char cat[ACCT_CAT_MAX];
        if (!Ui_ComboGetText(GetDlgItem(h, 103), cat, ACCT_CAT_MAX)) {
            Ui_MsgBox(h, L"提示", L"请选择分类。", MSG_WARN);
            SetFocus(GetDlgItem(h, 103));
            return 1;
        }
        rec->amount = amount;
        strncpy(rec->category, cat, ACCT_CAT_MAX - 1);
        rec->category[ACCT_CAT_MAX - 1] = 0;
        GetWindowTextW(GetDlgItem(h, 105), wbuf, 32);
        char date[32];
        wchar_to_utf8(wbuf, date, 32);
        normalize_date(date);
        if (!is_valid_date(date)) {
            Ui_MsgBox(h, L"日期无效", L"日期格式不正确，请按 YYYY-MM-DD 填写。", MSG_ERROR);
            SetFocus(GetDlgItem(h, 105));
            return 1;
        }
        snprintf(rec->date, ACCT_DATE_LEN, "%s", date);
        GetWindowTextW(GetDlgItem(h, 107), wbuf, 256);
        wchar_to_utf8(wbuf, rec->note, ACCT_NOTE_MAX);
        Ui_DlgFinish(h, 1);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

void ShowEditAcctDlg(HWND owner, AccountRecord* rec) {
    enum { W = 540, H = 262 };
    HWND d = Ui_DlgBegin(owner, L"编辑记账记录", W, H, edit_acct_proc);
    if (!d) return;
    SetPropW(d, L"ctx", (HANDLE)rec);

    Ui_DlgLabel(d, L"金额", 20, 52, 60, 18);
    HWND e1 = Ui_DlgEditCenter(d, 101, 20, 76, 150, 34);
    Ui_AmountEditGuard(e1);
    Ui_DlgLabel(d, L"分类", 185, 52, 60, 18);
    {
        HINSTANCE dInst = (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE);
        HWND c2 = Ui_ComboCreate(d, 103, dInst, kStdCategories, ACCT_STD_CAT_N);
        if (c2) {
            SetWindowPos(c2, NULL, Ui_S(185), Ui_S(76), Ui_S(150), Ui_S(34), SWP_NOZORDER);
            Ui_ComboSetColorize(c2, 1);
            /* 老数据里可能是 8 类之外的自定义分类，先补进列表再选中它 */
            Ui_ComboAddItem(c2, rec->category);
            if (!Ui_ComboSetText(c2, rec->category)) Ui_ComboSetSel(c2, 0);
        }
    }
    Ui_DlgLabel(d, L"日期", 350, 52, 60, 18);
    HWND e3 = Ui_DlgDateCenter(d, 105, 350, 76, 170, 34);
    Ui_DlgLabel(d, L"备注", 20, 118, 60, 18);
    HWND e4 = Ui_DlgEdit(d, 107, 20, 142, 500, 34, 0);
    SendMessageW(e4, EM_SETLIMITTEXT, UI_LIMIT_CHARS(ACCT_NOTE_MAX), 0);

    WCHAR wbuf[256];
    swprintf(wbuf, 256, L"%.2f", rec->amount);
    SetWindowTextW(e1, wbuf);
    {
        int y, m, dd;
        if (sscanf(rec->date, "%d/%d/%d", &y, &m, &dd) == 3)
            swprintf(wbuf, 256, L"%04d-%02d-%02d", y, m, dd);
        else
            utf8_to_wchar(rec->date, wbuf, 256);
        SetWindowTextW(e3, wbuf);
    }
    utf8_to_wchar(rec->note, wbuf, 256);
    SetWindowTextW(e4, wbuf);

    Ui_DlgButton(d, IDCANCEL, L"取消", DLG_CANCEL_X(W), DLG_BTN_Y(H), DLG_BTN_W_NO, DLG_BTN_H, 0);
    Ui_DlgButton(d, IDOK,     L"保存", DLG_OK_X(W),     DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 1);
    Ui_DlgRun(d);
}

/* ---- 3. 设置提醒 ---- */
static int reminder_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    int id = LOWORD(wp);
    Todo* t = dlg_of_todo(h);
    if (!t) return 0;

    if (id == 201 || id == 202 || id == 203) {
        int add = (id == 201) ? 15 * 60 : (id == 202) ? 30 * 60 : 60 * 60;
        time_t now = time(NULL) + add;
        struct tm* tm_now = localtime(&now);
        strftime(t->reminder, sizeof(t->reminder), "%Y-%m-%dT%H:%M:%S", tm_now);
        Ui_DlgFinish(h, 1);
        return 1;
    }
    if (id == IDOK) {
        WCHAR wbuf[32];
        char date_str[16], time_str[8];
        GetWindowTextW(GetDlgItem(h, 101), wbuf, 32);
        wchar_to_utf8(wbuf, date_str, 16);
        normalize_date(date_str);
        if (!is_valid_date(date_str)) {
            Ui_MsgBox(h, L"日期无效", L"日期格式不正确，请按 YYYY-MM-DD 填写。", MSG_ERROR);
            SetFocus(GetDlgItem(h, 101));
            return 1;
        }

        GetWindowTextW(GetDlgItem(h, 105), wbuf, 32);
        wchar_to_utf8(wbuf, time_str, 8);
        if (!time_str[0]) {
            strcpy(time_str, "00:00");
        } else {
            int hh = -1, mm = -1;
            if (sscanf(time_str, "%d:%d", &hh, &mm) != 2 ||
                hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                Ui_MsgBox(h, L"时间无效", L"时间请按 24 小时制 HH:MM 填写（如 09:30）。", MSG_ERROR);
                SetFocus(GetDlgItem(h, 105));
                return 1;
            }
            snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, mm);
        }

        char dt_str[32];
        snprintf(dt_str, sizeof(dt_str), "%sT%s:00", date_str, time_str);
        strncpy(t->reminder, dt_str, TODO_DATE_STR_LEN - 1);
        t->reminder[TODO_DATE_STR_LEN - 1] = 0;
        Ui_DlgFinish(h, 1);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

void ShowReminderDlg(HWND owner, Todo* t) {
    enum { W = 450, H = 250 };
    HWND d = Ui_DlgBegin(owner, L"设置提醒", W, H, reminder_proc);
    if (!d) return;
    SetPropW(d, L"ctx", (HANDLE)t);

    Ui_DlgLabel(d, L"日期", 20, 52, 60, 18);
    HWND e1 = Ui_DlgDateCenter(d, 101, 20, 76, 190, 34);
    Ui_DlgLabel(d, L"时间", 234, 52, 60, 18);
    HWND e2 = Ui_DlgTime(d, 105, 234, 76, 120, 34);

    char today[16];
    WCHAR wt[32];
    get_today(today, sizeof(today));
    utf8_to_wchar(today, wt, 32);
    SetWindowTextW(e1, wt);

    char time_str[8];
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    strftime(time_str, sizeof(time_str), "%H:%M", tm_now);
    WCHAR wtime[16];
    utf8_to_wchar(time_str, wtime, 16);
    SetWindowTextW(e2, wtime);

    Ui_DlgButton(d, 201, L"15 分钟后", 20, 128, 128, 34, 0);
    Ui_DlgButton(d, 202, L"30 分钟后", 161, 128, 128, 34, 0);
    Ui_DlgButton(d, 203, L"1 小时后",  302, 128, 128, 34, 0);

    Ui_DlgButton(d, IDCANCEL, L"取消", DLG_CANCEL_X(W), DLG_BTN_Y(H), DLG_BTN_W_NO, DLG_BTN_H, 0);
    Ui_DlgButton(d, IDOK,     L"确定", DLG_OK_X(W),     DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 1);
    Ui_DlgRun(d);
}

/* ---- 3.5 设置城市（双击标题栏的城市温度打开） ---- */

/* 把某地区的城市填进城市下拉框；pick 非空则选中它（不在该地区就选第一个） */
static void city_fill(HWND d, int region, const char* pick) {
    HWND c = GetDlgItem(d, 101);
    if (!c) return;
    Ui_ComboClear(c);
    for (int i = 0; i < kWeatherCityCount; i++)
        if ((int)kWeatherCities[i].region == region)
            Ui_ComboAddItem(c, kWeatherCities[i].name);
    if (!(pick && pick[0]) || !Ui_ComboSetText(c, pick))
        Ui_ComboSetSel(c, 0);
}

static int city_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    int id   = LOWORD(wp);
    int note = HIWORD(wp);

    /* 换地区 → 城市列表重填 */
    if (id == 100 && note == CBN_SELCHANGE) {
        HWND rc = GetDlgItem(h, 100);
        char rn[32];
        int reg = WC_R_CN;
        if (rc && Ui_ComboGetText(rc, rn, sizeof(rn))) {
            for (int i = 0; i < WC_R_COUNT; i++)
                if (strcmp(kWeatherRegions[i], rn) == 0) { reg = i; break; }
        }
        city_fill(h, reg, NULL);
        return 1;
    }
    if (id == IDOK) {
        HWND c = GetDlgItem(h, 101);
        char name[64];
        if (c && Ui_ComboGetText(c, name, sizeof(name)))
            Weather_SetCity(name);
        Ui_DlgFinish(h, 1);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

static void ShowCityDlg(HWND owner) {
    enum { W = 400, H = 248 };
    HWND d = Ui_DlgBegin(owner, L"设置城市", W, H, city_proc);
    if (!d) return;

    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE);
    int reg = Weather_RegionOf(Weather_City());

    Ui_DlgLabel(d, L"地区", 20,  52, 60, 18);
    Ui_DlgLabel(d, L"城市", 166, 52, 60, 18);

    HWND rc = Ui_ComboCreate(d, 100, inst, NULL, 0);
    if (rc) {
        SetWindowPos(rc, NULL, Ui_S(20), Ui_S(76), Ui_S(130), Ui_S(34), SWP_NOZORDER);
        for (int i = 0; i < WC_R_COUNT; i++)
            Ui_ComboAddItem(rc, kWeatherRegions[i]);
        Ui_ComboSetSel(rc, reg);
    }
    HWND c = Ui_ComboCreate(d, 101, inst, NULL, 0);
    if (c) {
        SetWindowPos(c, NULL, Ui_S(166), Ui_S(76), Ui_S(W - 186), Ui_S(34), SWP_NOZORDER);
        city_fill(d, reg, Weather_City());
    }

    Ui_DlgLabel(d, L"标题栏上的气温每 30 分钟自动刷新一次，也可以在标题栏双击这里改", 20, 118, W - 40, 18);

    /* 当日概况：气象 + 高低温 + 湿度 + 空气质量（拿不到就整行不画）。
       带上城市名，否则切了地区还没保存时，看着像是新城市的天气 */
    {
        char s8[128], line[192];
        if (Weather_Summary(s8, sizeof(s8))) {
            _snprintf(line, sizeof(line), "%s：%s", Weather_City(), s8);
            line[sizeof(line) - 1] = 0;
            WCHAR ws[192];
            utf8_to_wchar(line, ws, 192);
            Ui_DlgLabel(d, ws, 20, 142, W - 40, 18);
        }
    }
    Ui_DlgLabel(d, L"国内走中国天气网，国外走 Open-Meteo（免 key）", 20, 168, W - 40, 18);

    Ui_DlgButton(d, IDCANCEL, L"取消", DLG_CANCEL_X(W), DLG_BTN_Y(H), DLG_BTN_W_NO, DLG_BTN_H, 0);
    Ui_DlgButton(d, IDOK,     L"保存", DLG_OK_X(W),     DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 1);
    Ui_DlgRun(d);
}

/* ---- 4. 统计报表（原分类统计与月度报表合并） ---- */
/* 当月出现过的分类个数：用来定报表窗口高度，避免分类少时下面留一大片空白 */
static int month_cat_count(AppState* app, const char* month) {
    if (!app || !month || !month[0]) return 0;
    char seen[64][64];
    int n = 0;
    for (int i = 0; i < app->accounts.count; i++) {
        const AccountRecord* r = &app->accounts.items[i];
        if (r->deleted || acct_is_income(r)) continue;   /* 分类表只列支出 */
        char im[16];
        get_month_from_date(r->date, im, sizeof(im));
        if (strcmp(im, month) != 0) continue;
        int found = 0;
        for (int j = 0; j < n; j++)
            if (strcmp(seen[j], r->category) == 0) { found = 1; break; }
        if (!found && n < 64) {
            strncpy(seen[n], r->category, 63);
            seen[n][63] = 0;
            n++;
        }
    }
    return n;
}

/* 月份偏移：从 m 挪 delta 个月（负数为往前），out 至少 8 字节 */
static void month_shift(const char* m, int delta, char* out) {
    int y = 0, mo = 0;
    out[0] = 0;
    if (!m || sscanf(m, "%d-%d", &y, &mo) != 2 || mo < 1 || mo > 12) return;
    int idx = y * 12 + (mo - 1) + delta;
    if (idx < 0) return;
    snprintf(out, 8, "%04d-%02d", idx / 12, idx % 12 + 1);
}

/* 近 12 个月支出趋势：最后一个点就是当前所选月份 */
static void trend_fill(AppState* app, const char* month) {
    char        ml[TREND_MAX][8];
    double      v[TREND_MAX];
    const char* lab[TREND_MAX];

    for (int i = 0; i < TREND_MAX; i++) {
        month_shift(month, i - (TREND_MAX - 1), ml[i]);
        v[i] = 0;
    }
    if (app && ml[TREND_MAX - 1][0]) {
        for (int i = 0; i < app->accounts.count; i++) {
            const AccountRecord* r = &app->accounts.items[i];
            if (r->deleted || acct_is_income(r)) continue;   /* 趋势只看支出 */
            char im[16];
            get_month_from_date(r->date, im, sizeof(im));
            for (int k = TREND_MAX - 1; k >= 0; k--) {
                if (strcmp(im, ml[k]) == 0) { v[k] += r->amount; break; }
            }
        }
    }
    for (int i = 0; i < TREND_MAX; i++) lab[i] = ml[i][0] ? ml[i] + 5 : "";
    Ui_TrendSetData(lab, v, TREND_MAX, TREND_MAX - 1);
}

/* 窗口高度和列表高度跟着「当月分类数」走：切到没数据的月份不该留一大片空白 */
static void monthly_layout(HWND h, AppState* app, const char* month) {
    enum { W = 1000, LIST_Y = 254, STAT_ROW_H = 30, STAT_HEAD_H = 26 };

    int ncat  = month_cat_count(app, month);
    int rows  = (ncat > 0 ? ncat : 1) + 3;              /* 支出合计 / 收入合计 / 结余 */
    int list_h = STAT_HEAD_H + rows * STAT_ROW_H + 6;
    if (list_h < STAT_HEAD_H + 4 * STAT_ROW_H)  list_h = STAT_HEAD_H + 4 * STAT_ROW_H;
    if (list_h > STAT_HEAD_H + 12 * STAT_ROW_H) list_h = STAT_HEAD_H + 12 * STAT_ROW_H;
    int H = LIST_Y + list_h + 20 + DLG_BTN_H + 20;

    HWND lv = GetDlgItem(h, 102);
    if (lv) SetWindowPos(lv, NULL, 0, 0, Ui_S(W - 40), Ui_S(list_h),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    HWND b1 = GetDlgItem(h, ID_STAT_DETAIL);
    HWND b2 = GetDlgItem(h, IDCANCEL);
    if (b1) SetWindowPos(b1, NULL, Ui_S(20), Ui_S(DLG_BTN_Y(H)), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (b2) SetWindowPos(b2, NULL, Ui_S(DLG_OK_X(W)), Ui_S(DLG_BTN_Y(H)), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rc;
    if (GetWindowRect(h, &rc))
        SetWindowPos(h, NULL, 0, 0, rc.right - rc.left, Ui_S(H),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void monthly_fill(HWND h, AppState* app, const char* month) {
    HWND hList = GetDlgItem(h, 102);
    SendMessageW(hList, LVM_DELETEALLITEMS, 0, 0);

    double expense = acct_list_month_sum(&app->accounts, month, "expense");
    double income  = acct_list_month_sum(&app->accounts, month, "income");

    char seen[64][64];
    int seen_count = 0;
    int rec_cnt = 0;
    for (int i = 0; i < app->accounts.count; i++) {
        const AccountRecord* r = &app->accounts.items[i];
        if (r->deleted) continue;
        char item_month[16];
        get_month_from_date(r->date, item_month, sizeof(item_month));
        if (strcmp(item_month, month) != 0) continue;
        rec_cnt++;
        if (acct_is_income(r)) continue;            /* 收入不进分类占比 */
        int found = 0;
        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen[j], r->category) == 0) { found = 1; break; }
        }
        if (!found && seen_count < 64) {
            strncpy(seen[seen_count], r->category, 63);
            seen[seen_count][63] = 0;
            seen_count++;
        }
    }

    int seen_col[ACCT_CC_MAX];
    cat_colors_build(seen, seen_count, seen_col);

    const char* pie_nm[PIE_MAX];
    double      pie_pc[PIE_MAX];
    int         pie_n = 0;

    for (int i = 0; i < seen_count; i++) {
        double cat_total = cat_month_expense(app, seen[i], month);
        double pct = expense > 0 ? cat_total / expense * 100.0 : 0;
        char amt[64], pcts[32], bud_s[64];
        fmt_money_utf8(amt, sizeof(amt), cat_total, 2);
        snprintf(pcts, sizeof(pcts), "%.1f%%", pct);

        /* 预算列：超了标红（bit30），没设预算就画个破折号 */
        DWORD lp = ((DWORD)seen_col[i] & 0xF) << 25;
        double bud = budget_list_get(&app->budgets, seen[i]);
        if (bud > 0) {
            double left = bud - cat_total;
            if (left < -0.005) {
                char lb[48];
                fmt_money_utf8(lb, sizeof(lb), -left, 0);
                snprintf(bud_s, sizeof(bud_s), "超 %s", lb);
                lp |= (1u << 30);
            } else {
                char lb[48];
                fmt_money_utf8(lb, sizeof(lb), left, 0);
                snprintf(bud_s, sizeof(bud_s), "余 %s", lb);
            }
        } else {
            snprintf(bud_s, sizeof(bud_s), "—");
        }

        WCHAR wc[128], wa[64], wp[32], wb[64];
        utf8_to_wchar(seen[i], wc, 128);
        utf8_to_wchar(amt, wa, 64);
        utf8_to_wchar(pcts, wp, 32);
        utf8_to_wchar(bud_s, wb, 64);
        lv_add_row4(hList, wc, wa, wp, wb, lp);

        if (pie_n < PIE_MAX) {
            pie_nm[pie_n] = seen[i];
            pie_pc[pie_n] = pct;
            pie_n++;
        }
    }

    /* 环形图只画支出占比 */
    Ui_PieSetData(pie_nm, pie_pc, seen_col, pie_n, expense);

    {
        char amt[64];
        WCHAR wc[128], wa[64];

        fmt_money_utf8(amt, sizeof(amt), expense, 2);
        utf8_to_wchar("支出合计", wc, 128);
        utf8_to_wchar(amt, wa, 64);
        lv_add_row4(hList, wc, wa, L"100.0%", L"", (DWORD)ACCT_CI_NONE << 25);

        fmt_money_utf8(amt, sizeof(amt), income, 2);
        utf8_to_wchar("收入合计", wc, 128);
        utf8_to_wchar(amt, wa, 64);
        lv_add_row4(hList, wc, wa, L"—", L"", (DWORD)ACCT_CI_NONE << 25);

        fmt_money_utf8(amt, sizeof(amt), income - expense, 2);
        utf8_to_wchar("本月结余", wc, 128);
        utf8_to_wchar(amt, wa, 64);
        lv_add_row4(hList, wc, wa, L"—", L"", (DWORD)ACCT_CI_NONE << 25);

        /* 左下大数字：结余（收入 - 支出），下面一行小字是笔数 */
        HWND hSum = GetDlgItem(h, 106);
        if (hSum) {
            char sb[64];
            fmt_money_utf8(sb, sizeof(sb), income - expense, 2);
            WCHAR wsb[64];
            utf8_to_wchar(sb, wsb, 64);
            SetWindowTextW(hSum, wsb);
        }
        HWND hCnt = GetDlgItem(h, 108);      /* 107 是趋势图，别再撞 */
        if (hCnt) {
            char cb[64];
            snprintf(cb, sizeof(cb), "%d \xE7\xAC\x94", rec_cnt);
            WCHAR wcb[64];
            utf8_to_wchar(cb, wcb, 64);
            SetWindowTextW(hCnt, wcb);
        }
    }

    /* 右侧趋势图：近 12 个月，跟着所选月份整体平移 */
    trend_fill(app, month);
    monthly_layout(h, app, month);
}

static int monthly_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_COMMAND) return 0;
    int id = LOWORD(wp);
    if (id == 101 && HIWORD(wp) == EN_CHANGE) {
        if (!GetDlgItem(h, 102)) return 1;
        AppState* app = dlg_of_app(h);
        WCHAR wbuf[32];
        GetWindowTextW(GetDlgItem(h, 101), wbuf, 32);
        char month[16];
        wchar_to_utf8(wbuf, month, 16);
        if (app && strlen(month) == 7) monthly_fill(h, app, month);
        return 1;
    }
    if (id == ID_STAT_DETAIL) {
        AppState* app = dlg_of_app(h);
        WCHAR wbuf[32];
        GetWindowTextW(GetDlgItem(h, 101), wbuf, 32);
        char month[16];
        wchar_to_utf8(wbuf, month, 16);
        if (app && month[0]) {
            strncpy(app->acct_month, month, sizeof(app->acct_month) - 1);
            app->acct_month[sizeof(app->acct_month) - 1] = 0;
        }
        Ui_DlgFinish(h, 2);
        return 1;
    }
    if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    return 0;
}

void ShowMonthlyDlg(HWND owner, AppState* app) {
    enum { W = 1000, LIST_Y = 254, TOP_Y = 40, TOP_H = 200,
           STAT_ROW_H = 30, STAT_HEAD_H = 26 };

    char month[16];
    if (app && app->acct_month[0]) {
        strncpy(month, app->acct_month, sizeof(month) - 1);
        month[sizeof(month) - 1] = 0;
    } else {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        strftime(month, sizeof(month), "%Y-%m", tm_now);
    }

    /* 高度跟着行数走：分类少的时候不留一大片空白，也不会撑出屏幕 */
    int ncat  = month_cat_count(app, month);
    int rows  = (ncat > 0 ? ncat : 1) + 3;              /* 支出合计 / 收入合计 / 结余 */
    int list_h = STAT_HEAD_H + rows * STAT_ROW_H + 6;
    if (list_h < STAT_HEAD_H + 4 * STAT_ROW_H)  list_h = STAT_HEAD_H + 4 * STAT_ROW_H;
    if (list_h > STAT_HEAD_H + 12 * STAT_ROW_H) list_h = STAT_HEAD_H + 12 * STAT_ROW_H;
    int H = LIST_Y + list_h + 20 + DLG_BTN_H + 20;

    HWND d = Ui_DlgBegin(owner, L"统计报表", W, H, monthly_proc);
    if (!d) return;
    SetPropW(d, L"ctx2", (HANDLE)app);

    /* 月份改完立刻刷新，不再需要「查询」按钮：选了就是查了 */
    Ui_DlgLabel(d, L"月份", 20, 44, 60, 18);
    HWND e1 = Ui_DlgMonth(d, 101, 20, 68, 150, 34);

    WCHAR wm[32];
    utf8_to_wchar(month, wm, 32);
    SetWindowTextW(e1, wm);

    /* 左下：当月结余（大字）+ 记录笔数（小字） */
    Ui_DlgLabel(d, L"本月结余", 20, 116, 150, 18);
    {
        HWND hSum = Ui_DlgLabel(d, L"¥0.00", 20, 136, 160, 34);
        SendMessageW(hSum, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_TITLE), TRUE);
        SetWindowLongPtrW(hSum, GWLP_ID, 106);   /* monthly_fill 按 id 更新 */
    }
    {
        HWND hCnt = Ui_DlgLabel(d, L"0 笔", 20, 172, 150, 20);
        SetWindowLongPtrW(hCnt, GWLP_ID, 108);
    }

    /* 中间环形图看结构，右侧趋势图看走势：两者不重复 */
    Ui_PieCreate(d, 105, 190, TOP_Y, 380, TOP_H);
    Ui_TrendCreate(d, 107, 580, TOP_Y, W - 20 - 580, TOP_H);

    HWND lv = Ui_DlgList(d, 102, 20, LIST_Y, W - 40, list_h);
    {
        static const WCHAR* nm[4] = { L"分类", L"金额", L"占比", L"预算" };
        static const int ws[4] = { 370, 240, 150, 160 };
        static const int rt[4] = { 0, 1, 1, 1 };
        lv_cols(lv, nm, ws, rt, 4);
        /* 第 0 列画分类色块，与主界面记账列表同色 */
        SetPropW(lv, L"chipCol", (HANDLE)(INT_PTR)1);
    }

    Ui_DlgButton(d, ID_STAT_DETAIL, L"明细", 20, DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 0);
    Ui_DlgButton(d, IDCANCEL, L"关闭", DLG_OK_X(W), DLG_BTN_Y(H), DLG_BTN_W_OK, DLG_BTN_H, 0);

    monthly_fill(d, app, month);
    int r = Ui_DlgRun(d);
    if (r == 2 && app) {
        HWND top = GetAncestor(owner, GA_ROOT);
        if (!top) top = owner;
        SwitchTab(top, app, 1);
    }
}

/* 汇总卡右上角的月份胶囊：弹月份选择器，选完立刻刷新列表与汇总 */
static void PickAcctMonth(HWND panel, AppState* app) {
    if (!panel || !app) return;
    char cur_m[16];
    if (app->acct_month[0]) {
        strncpy(cur_m, app->acct_month, sizeof(cur_m) - 1);
        cur_m[sizeof(cur_m) - 1] = 0;
    } else {
        char today[DATE_STR_LEN];
        cur_m[0] = 0;
        get_today(today, sizeof(today));
        get_month_from_date(today, cur_m, sizeof(cur_m));
    }

    RECT mr = Ui_SumMonthRect(panel);
    POINT pt = { mr.left, mr.bottom + Ui_S(4) };
    ClientToScreen(panel, &pt);

    WCHAR wcur[16], wout[16];
    utf8_to_wchar(cur_m, wcur, 16);
    if (Ui_PickMonth(panel, NULL, pt.x, pt.y, wcur, wout, 16)) {
        char out[16];
        wchar_to_utf8(wout, out, 16);
        if (out[0]) {
            strncpy(app->acct_month, out, sizeof(app->acct_month) - 1);
            app->acct_month[sizeof(app->acct_month) - 1] = 0;
        }
        RefreshAccountTab(app);
        InvalidateRect(panel, NULL, FALSE);
    }
}

LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

#ifdef DIAG_MSG
    if (msg >= 0x0200 && msg <= 0x0210) {
        FILE* df = fopen("D:\\WorkBuddy\\_msg.log", "a");
        if (df) {
            fprintf(df, "%04X  x=%d y=%d\n", msg,
                    (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
            fclose(df);
        }
    }
#endif

    if (msg == WM_COMMAND || msg == WM_NOTIFY || msg == WM_CTLCOLORBTN ||
        msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_DRAWITEM ||
        msg == WM_CTLCOLORLISTBOX) {
        HWND parent = GetParent(hwnd);
        if (parent) return SendMessageW(parent, msg, wParam, lParam);
    }
    if (msg == WM_ERASEBKGND) return 1;

    /* 日期框右侧的日历图标不在 Edit 控件内，点到这块空白也要能弹出日历 */
    if (app && hwnd == app->hAcctPanel) {
        if (msg == WM_MOUSEMOVE) {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT mr = Ui_SumMonthRect(hwnd);
            int hv = PtInRect(&mr, p) ? 1 : 0;
            if (hv != app->hover_sum_mon) {
                app->hover_sum_mon = hv;
                InvalidateRect(hwnd, &mr, FALSE);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }

            /* 支出 / 收入 分段 */
            RECT tr = Ui_AcctTypeRect(hwnd);
            int th = 0;
            if (PtInRect(&tr, p))
                th = (p.x < (tr.left + tr.right) / 2) ? 1 : 2;
            if (th != app->hover_acct_type) {
                app->hover_acct_type = th;
                InvalidateRect(hwnd, &tr, FALSE);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        } else if (msg == WM_MOUSELEAVE) {
            if (app->hover_sum_mon || app->hover_acct_type) {
                app->hover_sum_mon = 0;
                app->hover_acct_type = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (msg == WM_LBUTTONDOWN) {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT mr = Ui_SumMonthRect(hwnd);
            if (PtInRect(&mr, p)) {
                PickAcctMonth(hwnd, app);
                return 0;
            }
            RECT tr = Ui_AcctTypeRect(hwnd);
            if (PtInRect(&tr, p)) {
                int t = (p.x < (tr.left + tr.right) / 2) ? 0 : 1;
                if (t != app->acct_type) {
                    app->acct_type = t;
                    InvalidateRect(hwnd, &tr, FALSE);
                }
                if (app->hAcctAmount) SetFocus(app->hAcctAmount);
                return 0;
            }
        }
    }

    /* 待办页的四象限：hover / 单击勾选 / 选中 / 双击编辑 / 右键菜单 / 块内滚轮 */
    if (app && hwnd == app->hTodoPanel) {
        if (msg == WM_MOUSEMOVE) {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int row = -1;
            int q = Ui_QuadHit(app, p, &row);
            if (q != app->quad_hover || row != app->quad_hover_row) {
                app->quad_hover = q;
                app->quad_hover_row = (q >= 0) ? row : -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        } else if (msg == WM_MOUSELEAVE) {
            if (app->quad_hover >= 0 || app->quad_hover_row >= 0) {
                app->quad_hover = -1;
                app->quad_hover_row = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (msg == WM_LBUTTONDOWN) {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int row = -1;
            int q = Ui_QuadHit(app, p, &row);
            if (q >= 0 && row >= 0 && row < app->quad_n[q]) {
                int id = app->quad_ids[q][row];
                RECT rr;
                Ui_QuadRowRect(app, q, row, &rr);
                /* 点在行左侧的勾选圈里 = 切换完成；点别处只选中 */
                if (p.x <= rr.left + Ui_S(34)) {
                    ToggleTodoItem(app, id);
                    return 0;
                }
                /* 双击兜底：系统没发 WM_LBUTTONDBLCLK 时，靠「同一行 + 双击间隔内 +
                   双击矩形内」自己认一次。系统正常发 DBLCLK 时走不到这里（第二下
                   变成 DBLCLK，不是 DOWN），两条路互不重叠。 */
                {
                    DWORD now = GetTickCount();
                    int is_dbl = (id == app->quad_last_click_id) &&
                                 ((DWORD)(now - app->quad_last_click_tick)
                                      <= (DWORD)GetDoubleClickTime()) &&
                                 (abs(p.x - app->quad_last_click_pt.x)
                                      <= GetSystemMetrics(SM_CXDOUBLECLK) / 2) &&
                                 (abs(p.y - app->quad_last_click_pt.y)
                                      <= GetSystemMetrics(SM_CYDOUBLECLK) / 2);
                    app->quad_last_click_tick = now;
                    app->quad_last_click_pt   = p;
                    if (is_dbl) {
                        app->quad_last_click_id = 0;   /* 用掉就清，免得连累下一次单击 */
                        EditTodoItem(hwnd, app, id);
                        return 0;
                    }
                    app->quad_last_click_id = id;
                }
                app->quad_sel_id = (app->quad_sel_id == id) ? 0 : id;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        } else if (msg == WM_LBUTTONDBLCLK) {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int row = -1;
            int q = Ui_QuadHit(app, p, &row);
#ifdef DIAG_MSG
            {
                RECT qr = (q >= 0) ? app->rt_quad[q] : app->rt_quad[0];
                FILE* df = fopen("D:\\WorkBuddy\\_hit.log", "a");
                if (df) {
                    fprintf(df, "DBLCLK p=(%d,%d) q=%d row=%d n=%d,%d,%d,%d rv=%d "
                                "rect=(%ld,%ld)-(%ld,%ld) scroll=%d\n",
                            (int)p.x, (int)p.y, q, row,
                            app->quad_n[0], app->quad_n[1], app->quad_n[2], app->quad_n[3],
                            app->quad_rows_vis,
                            qr.left, qr.top, qr.right, qr.bottom,
                            (q >= 0) ? app->quad_scroll[q] : -1);
                    fclose(df);
                }
            }
#endif
            app->quad_last_click_id = 0;   /* 系统已按双击处理，清掉兜底状态 */
            if (q >= 0 && row >= 0 && row < app->quad_n[q])
                EditTodoItem(hwnd, app, app->quad_ids[q][row]);
            return 0;
        } else if (msg == WM_RBUTTONDOWN) {
            HandleTodoRClick(hwnd, app, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
            return 0;
        } else if (msg == WM_MOUSEWHEEL) {
            /* 鼠标在哪个块里就滚哪个块 */
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);
            int q = Ui_QuadHit(app, p, NULL);
            if (q >= 0) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                int maxs = app->quad_n[q] - app->quad_rows_vis;
                if (maxs > 0) {
                    int step = (delta > 0) ? -1 : 1;
                    int ns = app->quad_scroll[q] + step;
                    if (ns < 0) ns = 0;
                    if (ns > maxs) ns = maxs;
                    if (ns != app->quad_scroll[q]) {
                        app->quad_scroll[q] = ns;
                        InvalidateRect(hwnd, &app->rt_quad[q], FALSE);
                    }
                }
                return 0;
            }
        }
    }

    if (msg == WM_LBUTTONDOWN && app && hwnd == app->hAcctPanel && app->hAcctDate) {
        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);
        RECT er;
        if (GetWindowRect(app->hAcctDate, &er)) {
            POINT p1 = { er.left, er.top };
            POINT p2 = { er.right, er.bottom };
            ScreenToClient(hwnd, &p1);
            ScreenToClient(hwnd, &p2);
            int pad = Ui_S(UI_DATE_CAL_PAD) + Ui_S(UI_INPUT_INSET_X);
            if (mx >= p2.x - 4 && mx <= p2.x + pad && my >= p1.y && my <= p2.y) {
                Ui_DateEditClick(app->hAcctDate);
                return 0;
            }
        }
    }

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0 && app) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
            if (hwnd == app->hTimerBar)
                Ui_PaintTimerBar(app, mem);
            else
                Ui_PaintPanel(app, hwnd, mem);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static AppState* app = NULL;

    switch (msg) {
    case WM_CREATE: {
        app = (AppState*)((CREATESTRUCT*)lParam)->lpCreateParams;
        app->hInst = ((CREATESTRUCT*)lParam)->hInstance;

        INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&icex);

        Ui_Init();
        CreateAppDataPath(app, app->hInst);
        Theme_Init(app->config_path);
        /* 天气在后台线程里拉，断网也只是不显示，不会卡启动 */
        Weather_Init(hwnd, WM_APP_WEATHER);
        todo_list_init(&app->todos);
        acct_list_init(&app->accounts);
        budget_list_init(&app->budgets);
        app->next_todo_id = 1;
        app->next_acct_id = 1;
        app->acct_type = 0;
        app->hover_acct_type = 0;
        app->read_only = 0;
        app->data_broken = 0;
        app->current_tab = 0;
        app->timer_running = 0;
        app->timer_paused = 0;
        app->timer_seconds = 0;
        app->timer_total = 0;
        app->hover_sum_mon = 0;
        {
            /* 记账列表默认只看当月 */
            char today[DATE_STR_LEN];
            app->acct_month[0] = 0;
            get_today(today, sizeof(today));
            get_month_from_date(today, app->acct_month, sizeof(app->acct_month));
        }
        LoadData(app);

        Ui_BuildControls(app, hwnd);

        RECT rc;
        GetClientRect(hwnd, &rc);
        Ui_Layout(app, hwnd, rc.right, rc.bottom);

        UpdateStatusBar(app);
        ShowTrayIcon(hwnd, app->hInst);
        SetTimer(hwnd, TIMER_CLOCK, 60000, NULL);
        SetTimer(hwnd, TIMER_COUNTDOWN, 1000, NULL);
        SetTimer(hwnd, TIMER_REMINDER, 5000, NULL);
        SwitchTab(hwnd, app, 0);
        app->initialized = 1;
        Ui_ApplyTheme(app);
        if (app->data_broken) PostMessageW(hwnd, WM_APP + 5, 0, 0);
        /* 开机自启：按 config 里的意图校正注册表（第一次跑自动注册；exe 挪过目录、
           或者注册项还是老 Python 版留下的死链，都会在这里重写成当前路径） */
        Startup_Ensure();
        /* 把「程序没开时错过的提醒」丢进消息队列，等窗口彻底起来再弹 */
        PostMessageW(hwnd, WM_APP_REMINDER_BOOT, 0, 0);
        app->initialized = 1;
        break;
    }

    case WM_SIZE: {
        if (!app || !app->initialized) break;
        RECT rc;
        GetClientRect(hwnd, &rc);
        Ui_Layout(app, hwnd, rc.right, rc.bottom);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    /* 数据文件损坏：窗口起来之后再弹，免得 WM_CREATE 里模态把创建流程卡住 */
    case WM_APP + 5:
        if (app) {
            Ui_MsgBox(hwnd, L"数据文件异常",
                      L"数据文件存在但无法解析，已进入只读模式。\n\n"
                      L"为避免把原文件覆盖掉，本次运行不会保存任何改动。\n"
                      L"修复办法：\n"
                      L"1. 关闭程序；\n"
                      L"2. 打开 %APPDATA%\\TodoApp\\，把 accounts.json / todos.json 里损坏的那个改名备份；\n"
                      L"3. 从同目录的 backups\\ 文件夹里挑一份最近的备份，改回原名；\n"
                      L"4. 重新启动程序。",
                      MSG_ERROR);
        }
        break;

    /* 启动补报：程序没运行期间到点的提醒，起来后汇总弹一次 */
    case WM_APP_REMINDER_BOOT:
        if (app) {
            app->initialized = 1;      /* Ui_MsgBoxFront 要布局就绪 */
            Reminder_Fire(app, hwnd, 1);
        }
        break;

    /* 快捷键（main.c 的消息循环转发过来） */
    case WM_APP + 6: {
        if (!app) break;
        UINT c = (UINT)wParam;
        if (c == 'N') {
            if (app->current_tab == 0) { if (app->hTodoTitle) SetFocus(app->hTodoTitle); }
            else                       { if (app->hAcctAmount) SetFocus(app->hAcctAmount); }
        } else if (c == 'F') {
            HWND s = (app->current_tab == 0) ? app->hTodoSearch : app->hAcctSearch;
            if (s) { SetFocus(s); SendMessageW(s, EM_SETSEL, 0, (LPARAM)-1); }
        } else if (c == 'D') {
            ShowTrashDlg(hwnd, app);
        } else if (c == VK_ESCAPE) {
            HWND s = (app->current_tab == 0) ? app->hTodoSearch : app->hAcctSearch;
            if (s) {
                WCHAR cur[128];
                GetWindowTextW(s, cur, 128);
                if (cur[0]) {
                    SetWindowTextW(s, L"");
                    RefreshTodoTab(app);
                    RefreshAccountTab(app);
                }
            }
        }
        break;
    }

#ifdef CAP_HOOK
    /* 仅供离线截图脚本使用：屏幕锁定时无法注入真实鼠标，改为投递坐标 */
    case WM_APP + 9: {
        if (!app) break;
        int hx = (short)LOWORD(wParam);
        int hy = (short)HIWORD(wParam);
        if (lParam == 1) HandleAccountListRClick(hwnd, app, hx, hy);
        else             HandleTodoRClick(hwnd, app, hx, hy);
        break;
    }
#endif

    case WM_NOTIFY: {
        if (!app) break;
        LPNMHDR hdr = (LPNMHDR)lParam;
        if (hdr->code == NM_CUSTOMDRAW)
            return Ui_ListCustomDraw((LPNMLVCUSTOMDRAW)lParam);

        /* 待办页已经不是 ListView 了，这里只剩记账列表的通知 */
        if (hdr->code == NM_DBLCLK) {
            if (hdr->idFrom == ID_ACCT_LIST)
                HandleAccountListDblClick(hwnd, app);
        }
        if (hdr->code == NM_RCLICK) {
            if (hdr->idFrom == ID_ACCT_LIST) {
                LVHITTESTINFO ht;
                ht.pt.x = (short)LOWORD(GetMessagePos());
                ht.pt.y = (short)HIWORD(GetMessagePos());
                ScreenToClient(hdr->hwndFrom, &ht.pt);
                HandleAccountListRClick(hwnd, app, ht.pt.x, ht.pt.y);
                return TRUE;
            }
        }
        break;
    }

    case WM_COMMAND: {
        if (!app) break;
        int id = LOWORD(wParam);

        if (id == ID_TODO_ADD) { AddTodoItem(app); }
        else if (id == ID_TODO_PRIO_COMBO) {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = Ui_ComboGetSel(app->hTodoPrioCombo);
                if (sel >= 0 && sel < TODO_QUAD_COUNT)
                    app->todo_prio = TODO_QUAD_PRIO(sel);
            }
        }
        else if (id == ID_TODO_REFRESH) { RefreshTodoTab(app); }
        else if (id == ID_ACCT_ADD) { AddAccountItem(app); }
        else if (id == ID_ACCT_REFRESH) { RefreshAccountTab(app); }
        else if (id == ID_ACCT_MONTHLY) { ShowMonthlyReport(hwnd, app); }
        else if (id == ID_TODO_SEARCH || id == ID_ACCT_SEARCH) {
            if (HIWORD(wParam) == EN_CHANGE) {
                if (id == ID_TODO_SEARCH) RefreshTodoTab(app);
                else RefreshAccountTab(app);
            }
        }
        else if (id == ID_TRAY_SHOW) {
            /* 最小化时 SW_SHOW 不会还原（窗口还是缩在任务栏），必须 SW_RESTORE */
            ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (id == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
        }
        else if (id == ID_TRAY_AUTOSTART) {
            Startup_Apply(!Startup_IsEnabled());
        }
        else if (id == ID_MENU_BACKUP) { BackupData(hwnd, app); }
        else if (id == ID_MENU_IMPORT) { ImportData(hwnd, app); }
        else if (id == ID_MENU_EXPORT_CSV) { ExportCsvData(hwnd, app); }
        else if (id == ID_MENU_TRASH) { ShowTrashDlg(hwnd, app); }
        else if (id == ID_MENU_BUDGET) { ShowBudgetDlg(hwnd, app); }
        else if (id == ID_TMR_START) { StartTimerCountdown(app); }
        else if (id == ID_TMR_PAUSE) { PauseTimer(app); }
        else if (id == ID_TMR_RESET) { ResetTimer(app); }
        break;
    }

    case TRAY_MSG: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        break;
    }

    case WM_TIMER: {
        if (!app) break;
        if (wParam == TIMER_CLOCK) {
            UpdateStatusBar(app);
        } else if (wParam == TIMER_COUNTDOWN) {
            if (app->timer_running && !app->timer_paused && app->timer_seconds > 0) {
                app->timer_seconds--;
                UpdateTimerDisplayLabel(app);
                if (app->timer_seconds == 0) {
                    app->timer_running = 0;
                    NotifyTimerDone(hwnd, app);
                }
            }
        } else if (wParam == TIMER_REMINDER) {
            Reminder_Fire(app, hwnd, 0);
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    /* ---- 无边框窗口（自绘标题栏）---- */

    case WM_NCCALCSIZE:
        /* wParam=TRUE 时返回 0：客户区 = 整个窗口，彻底去掉系统标题栏/边框 */
        if (wParam) return 0;
        break;

    case WM_NCACTIVATE:
        /* 不画非客户区，避免激活切换时边缘闪白 */
        return TRUE;

    case WM_NCHITTEST: {
        if (!app) break;
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hwnd, &pt);

        RECT rc;
        GetClientRect(hwnd, &rc);

        int top = Ui_S(UI_TOPBAR_H);

        /* 顶部栏上的可交互热区始终按客户区处理 */
        if (pt.y >= 0 && pt.y < top && Ui_HitTopbar(app, pt) != HIT_NONE)
            return HTCLIENT;

        if (IsZoomed(hwnd)) {
            if (pt.y >= 0 && pt.y < top) return HTCAPTION;
            return HTCLIENT;
        }

        /* 四边/四角缩放区（与系统原生手感一致） */
        {
            int b = Ui_S(5);
            int on_l = pt.x < b;
            int on_r = pt.x >= rc.right - b;
            int on_t = pt.y < b;
            int on_b = pt.y >= rc.bottom - b;

            if (on_t && on_l) return HTTOPLEFT;
            if (on_t && on_r) return HTTOPRIGHT;
            if (on_b && on_l) return HTBOTTOMLEFT;
            if (on_b && on_r) return HTBOTTOMRIGHT;
            if (on_l) return HTLEFT;
            if (on_r) return HTRIGHT;
            if (on_t) return HTTOP;
            if (on_b) return HTBOTTOM;
        }

        if (pt.y >= 0 && pt.y < top) return HTCAPTION;  /* 拖动 / 双击最大化 */
        return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        int dpi = Ui_Dpi();
        mmi->ptMinTrackSize.x = MulDiv(APP_MIN_WIDTH, dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(APP_MIN_HEIGHT, dpi, 96);

        /* 无客户区后，最大化需手动铺满显示器工作区 */
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        if (!app) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
            Ui_PaintMain(app, hwnd, mem);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_MOUSEMOVE: {
        if (!app) break;
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        int hit = Ui_HitTopbar(app, pt);
        if (hit != app->hover_hit) {
            app->hover_hit = hit;
            RECT rca;
            GetClientRect(hwnd, &rca);
            rca.bottom = Ui_S(UI_TOPBAR_H);
            InvalidateRect(hwnd, &rca, FALSE);
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
        break;
    }

    case WM_MOUSELEAVE:
        if (app && app->hover_hit != HIT_NONE) {
            app->hover_hit = HIT_NONE;
            RECT rca;
            GetClientRect(hwnd, &rca);
            rca.bottom = Ui_S(UI_TOPBAR_H);
            InvalidateRect(hwnd, &rca, FALSE);
        }
        break;

    case WM_APP_WEATHER: {
        /* 后台线程回来了：天气区宽度是按文字实测的，串变长/变短都要重新排一次，
           否则「上海 --°C」的旧矩形会把「上海 多云 19°C」裁掉半截 */
        if (app) {
            RECT rr;
            GetClientRect(hwnd, &rr);
            Ui_Layout(app, hwnd, rr.right, rr.bottom);
            rr.bottom = Ui_S(UI_TOPBAR_H);
            InvalidateRect(hwnd, &rr, FALSE);
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!app) break;
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        app->pressed_hit = HIT_NONE;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (Ui_HitTopbar(app, pt) == HIT_WEATHER) {
            ShowCityDlg(hwnd);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!app) break;
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        int hit = Ui_HitTopbar(app, pt);
        if (hit != HIT_NONE) {
            app->pressed_hit = hit;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (!app) break;
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        int hit = Ui_HitTopbar(app, pt);
        int pressed = app->pressed_hit;
        if (pressed) {
            app->pressed_hit = HIT_NONE;
            ReleaseCapture();
        }
        InvalidateRect(hwnd, NULL, FALSE);
        if (pressed && hit == pressed) {
            if (hit == HIT_FILE) {
                Ui_ShowFileMenu(app, hwnd);
            } else if (hit == HIT_THEME) {
                Theme_Toggle();
                Ui_ApplyTheme(app);
            } else if (hit == HIT_TAB0) {
                SwitchTab(hwnd, app, 0);
            } else if (hit == HIT_TAB1) {
                SwitchTab(hwnd, app, 1);
            } else if (hit == HIT_MIN) {
                ShowWindow(hwnd, SW_MINIMIZE);
            } else if (hit == HIT_MAX) {
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            } else if (hit == HIT_CLOSE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis && dis->CtlType == ODT_BUTTON) {
            Ui_DrawOwnerdrawButton(dis);
            return TRUE;
        }
        if (dis && dis->CtlType == ODT_MENU) {
            Ui_DrawMenuItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlType == ODT_MENU) {
            Ui_MeasureMenuItem(mis);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        const Theme* t = Theme_Now();
        SetTextColor((HDC)wParam, t->text);
        SetBkColor((HDC)wParam, t->input_bg);
        return (LRESULT)Ui_InputBrush();
    }

    case WM_CTLCOLORSTATIC: {
        const Theme* t = Theme_Now();
        SetTextColor((HDC)wParam, t->text_sub);
        SetBkColor((HDC)wParam, t->surface);
        return (LRESULT)Ui_InputBrush();
    }

    case WM_DESTROY: {
        if (app) {
            NOTIFYICONDATAW nid = {sizeof(nid), hwnd, ID_TRAY_ICON};
            Shell_NotifyIconW(NIM_DELETE, &nid);
            KillTimer(hwnd, TIMER_CLOCK);
            KillTimer(hwnd, TIMER_COUNTDOWN);
            KillTimer(hwnd, TIMER_REMINDER);
            Weather_Shutdown();
            todo_list_free(&app->todos);
            acct_list_free(&app->accounts);
            Ui_Shutdown();
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}
