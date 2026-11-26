#include "app_ui.h"
#include "ui.h"
#include "theme.h"
#include "util.h"
#include "calendar.h"
#include "menu.h"
#include "weather.h"
#include <commctrl.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

static HBRUSH     g_br_input = NULL;
static HIMAGELIST g_row_il   = NULL;

/* 颜色按百分比混合：pct = 往 b 靠多少（0 = 全 a，100 = 全 b） */
static COLORREF mix_color(COLORREF a, COLORREF b, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int r = (GetRValue(a) * (100 - pct) + GetRValue(b) * pct) / 100;
    int g = (GetGValue(a) * (100 - pct) + GetGValue(b) * pct) / 100;
    int bl = (GetBValue(a) * (100 - pct) + GetBValue(b) * pct) / 100;
    return RGB(r, g, bl);
}

/* 倒计时那种窄框专用的紧凑槽：四周只扩 3px。
 * 主面板那套 UI_INPUT_INSET_X=10 是给宽输入框留呼吸感的，
 * 套到 30px 的小框上会让槽宽翻倍，跟旁边的「时/分/秒」撞在一起。 */
#define UI_TIGHT_INSET 3

/* ---------------- 小工具 ---------------- */

static HWND make_edit(HWND parent, int id, HINSTANCE hInst, DWORD extra_style) {
    /*
     * 用「多行 + 居中文本带」实现单行输入框：原生单行 EDIT 的文字贴着框顶，
     * 控件高一点就显得偏上（见 Ui_EditVAlign 里的说明）。
     */
    HWND h = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | extra_style,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_BODY), TRUE);
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(Ui_S(9), Ui_S(9)));
    Ui_EditVAlign(h);
    return h;
}

static HWND make_btn(HWND parent, int id, const WCHAR* text, HINSTANCE hInst);

/* 日期输入框：单击即弹自绘日历，选完回填并触发 EN_CHANGE */
void Ui_DateEditClick(HWND h) {
    WCHAR cur[64] = {0};
    WCHAR out[16]  = {0};
    GetWindowTextW(h, cur, 64);
    HWND root = GetAncestor(h, GA_ROOT);
    if (Ui_PickDate(root ? root : GetParent(h), h, 0, 0, cur, out, 16)) {
        SetWindowTextW(h, out);
        SendMessageW(GetParent(h), WM_COMMAND,
                     MAKEWPARAM((WORD)GetDlgCtrlID(h), EN_CHANGE), (LPARAM)h);
    }
}

static LRESULT CALLBACK date_edit_subproc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)wp; (void)lp;
    if (msg == WM_LBUTTONDOWN) {
        Ui_DateEditClick(h);
        return 0;
    }
    (void)ref;
    return DefSubclassProc(h, msg, wp, lp);
}

/* ---------------- 金额输入校验 ---------------- */

/*
 * 金额框只接受 "1234" / "1234.5" / "1234.56" 这种形态：
 * 单个小数点、整数最多 7 位、小数最多 2 位。老板手快敲进字母也不会进框。
 */
static int amount_text_ok(const WCHAR* s, int len) {
    int dot = -1, intd = 0;
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i];
        if (c == L'.') {
            if (dot >= 0) return 0;          /* 第二个小数点 */
            dot = i;
            if (intd == 0) return 0;         /* 不允许 ".5"，必须先有整数位 */
        } else if (c >= L'0' && c <= L'9') {
            if (dot < 0) {
                if (++intd > 7) return 0;
            } else if (i - dot > 2) {
                return 0;                    /* 小数超过两位 */
            }
        } else {
            return 0;
        }
    }
    return 1;
}

/* 把 cand 里的字符按「插入到选区」的结果拼出来再校验 */
static int amount_try_insert(HWND h, WPARAM ch) {
    WCHAR cur[64] = {0};
    int len = GetWindowTextW(h, cur, 64);
    if (len < 0) len = 0;
    if (len > 60) len = 60;

    DWORD sel = (DWORD)SendMessageW(h, EM_GETSEL, 0, 0);
    int s0 = (int)LOWORD(sel), s1 = (int)HIWORD(sel);
    if (s1 < s0) { int t = s0; s0 = s1; s1 = t; }
    if (s0 > len) s0 = len;
    if (s1 > len) s1 = len;

    WCHAR cand[80];
    int k = 0;
    for (int i = 0; i < s0 && k < 76; i++) cand[k++] = cur[i];
    cand[k++] = (WCHAR)ch;
    for (int i = s1; i < len && k < 78; i++) cand[k++] = cur[i];
    cand[k] = 0;
    return amount_text_ok(cand, k);
}

static LRESULT CALLBACK amount_subproc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                       UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_CHAR:
        if (wp == VK_BACK || wp == VK_DELETE) break;
        if (wp < 32) break;                       /* 控制字符交给系统 */
        if (!((wp >= L'0' && wp <= L'9') || wp == L'.')) return 0;
        if (!amount_try_insert(h, wp)) return 0;
        break;

    case WM_PASTE: {
        if (!OpenClipboard(h)) break;
        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
        const WCHAR* src = hd ? (const WCHAR*)GlobalLock(hd) : NULL;
        if (!src) { CloseClipboard(); break; }

        /* 只留数字和第一个小数点，小数截到两位 */
        WCHAR clean[64];
        int k = 0, dot = -1, frac = 0;
        for (int i = 0; src[i] && k < 60; i++) {
            WCHAR c = src[i];
            if (c >= L'0' && c <= L'9') {
                if (dot >= 0) {
                    if (++frac > 2) continue;
                }
                clean[k++] = c;
            } else if (c == L'.' && dot < 0 && k > 0) {
                dot = k;
                clean[k++] = c;
            }
        }
        clean[k] = 0;
        GlobalUnlock(hd);
        CloseClipboard();

        if (k > 0 && amount_text_ok(clean, k))
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)clean);
        return 0;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void Ui_AmountEditGuard(HWND hEdit) {
    if (!hEdit) return;
    SendMessageW(hEdit, EM_SETLIMITTEXT, UI_AMOUNT_LIMIT, 0);
    SetWindowSubclass(hEdit, amount_subproc, 5, 0);
}

/* 自绘按钮的 hover 跟踪（BS_OWNERDRAW 不会自己维护 hover 态） */
static LRESULT CALLBACK btn_subproc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, L"hovered")) {
            SetPropW(h, L"hovered", (HANDLE)1);
            InvalidateRect(h, NULL, FALSE);
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = h;
            t.dwHoverTime = 0;
            TrackMouseEvent(&t);
        }
        break;
    case WM_MOUSELEAVE:
        if (GetPropW(h, L"hovered")) {
            RemovePropW(h, L"hovered");
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static HWND make_btn(HWND parent, int id, const WCHAR* text, HINSTANCE hInst) {
    HWND h = CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);
    SetWindowSubclass(h, btn_subproc, 1, 0);
    return h;
}

static HWND make_list(HWND parent, int id, HINSTANCE hInst) {
    HWND h = CreateWindowExW(0, WC_LISTVIEWW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
        LVS_NOSORTHEADER | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, NULL);
    ListView_SetExtendedListViewStyle(h,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(h, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);

    if (!g_row_il)
        g_row_il = ImageList_Create(1, Ui_S(32), ILC_COLOR32, 1, 1);
    if (g_row_il)
        SendMessageW(h, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_row_il);
    return h;
}

static void add_col(HWND hList, int idx, int width, int fmt) {
    LVCOLUMNW c;
    memset(&c, 0, sizeof(c));
    c.mask = LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    c.cx = width;
    c.iSubItem = idx;
    c.fmt = fmt;
    SendMessageW(hList, LVM_INSERTCOLUMNW, (WPARAM)idx, (LPARAM)&c);
}


/* 列宽计算基准：扣掉垂直滚动条宽度，避免出现横向滚动条 */
static int list_fit_width(int listw) {
    /* 原生纵向滚动条虽然不显示，控件内部仍会预留一条槽位，列表宽度要减掉它 */
    int w = listw - GetSystemMetrics(SM_CXVSCROLL) - Ui_S(2);
    if (w < Ui_S(200)) w = listw;
    return w;
}

static void calc_acct_cols(int listw, int c[4]) {
    c[0] = Ui_S(94);
    c[1] = Ui_S(84);
    c[2] = Ui_S(92);
    c[3] = listw - c[0] - c[1] - c[2];
    if (c[3] < Ui_S(140)) c[3] = Ui_S(140);
}

/* ---------------- 待办四象限：常量 ---------------- */
/*
 * 待办不再是「一张大列表」，而是 2×2 四个方块：重要且紧急 / 重要不紧急 /
 * 紧急不重要 / 不重要不紧急。每块一个浅色底，里面直接列条目。
 * 整块都是自绘（不用 ListView）：卡片底色、圆角、标题胶囊、行内勾选圆
 * 这些东西套 ListView 做不出来，硬加自绘层反而更绕。
 */

const WCHAR* const kQuadName[TODO_QUAD_COUNT] = {
    L"重要且紧急", L"重要不紧急", L"紧急不重要", L"不重要不紧急"
};
const char* const kQuadNameU8[TODO_QUAD_COUNT] = {
    "重要且紧急", "重要不紧急", "紧急不重要", "不重要不紧急"
};
/* 色号取自 ui.c 的分类色板：红 8 / 蓝 0 / 琥珀 6 / 石板 14。
 * Ui_CatChipColors 在浅色主题给「浅底 + 深字」、深色主题给「深底 + 亮字」，跟着主题走。 */
static const int kQuadCat[TODO_QUAD_COUNT] = { 8, 0, 6, 14 };

#define QUAD_HEAD_H 38   /* 卡片标题行高（字号调大了，行高跟着放开） */
#define QUAD_ROW_H  32   /* 条目行高 */

COLORREF Ui_QuadColor(int q) {
    if (q < 0 || q >= TODO_QUAD_COUNT) q = TODO_QUAD_COUNT - 1;
    COLORREF bg, fg;
    Ui_CatChipColors(kQuadCat[q], &bg, &fg);
    return fg;
}

static COLORREF quad_bg(int q) {
    if (q < 0 || q >= TODO_QUAD_COUNT) q = TODO_QUAD_COUNT - 1;
    COLORREF bg, fg;
    Ui_CatChipColors(kQuadCat[q], &bg, &fg);
    /* 深色主题下 chip 的 900 档底色是给「小色块」用的，铺满半屏就变成红砖 /
       电光蓝，扎眼。改成「面板底色 + 8% 语义色」，只留一点色调倾向，
       认颜色靠标题和圆点。 */
    if (Theme_Now()->dark) return mix_color(Theme_Now()->surface, fg, 11);
    return bg;
}

/* 次要文字（如「暂无事项」）：深色下把亮色压暗一档，别跟标题抢眼 */
static COLORREF quad_muted(int q) {
    const Theme* t = Theme_Now();
    if (!t->dark) return Ui_QuadColor(q);
    return mix_color(t->surface, Ui_QuadColor(q), 34);
}

/* ---------------- 创建控件 ---------------- */

void Ui_BuildControls(AppState* app, HWND hwnd) {
    HINSTANCE hInst = app->hInst;

    app->hTimerBar = CreateWindowW(TODO_PANEL_CLASS, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0, hwnd, NULL, hInst, NULL);
    SetWindowLongPtrW(app->hTimerBar, GWLP_USERDATA, (LONG_PTR)app);
    SetPropW(hwnd, L"TimerBarHWND", (HANDLE)app->hTimerBar);

    app->hTimerHour = make_edit(app->hTimerBar, ID_TMR_HOUR, hInst, ES_CENTER | ES_NUMBER);
    app->hTimerMin  = make_edit(app->hTimerBar, ID_TMR_MIN,  hInst, ES_CENTER | ES_NUMBER);
    app->hTimerSec  = make_edit(app->hTimerBar, ID_TMR_SEC,  hInst, ES_CENTER | ES_NUMBER);
    {
        /* 窄框：去掉 make_edit 默认的 9px 内边距，否则两位数字被挤出显示区 */
        HWND three[3] = { app->hTimerHour, app->hTimerMin, app->hTimerSec };
        for (int i = 0; i < 3; i++) {
            SendMessageW(three[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(Ui_S(3), Ui_S(3)));
            SendMessageW(three[i], EM_SETLIMITTEXT, 2, 0);
            Ui_EditVAlign(three[i]);
        }
    }
    SetWindowTextW(app->hTimerHour, L"0");
    SetWindowTextW(app->hTimerMin,  L"0");
    SetWindowTextW(app->hTimerSec,  L"0");

    make_btn(app->hTimerBar, ID_TMR_START, L"开始", hInst);
    make_btn(app->hTimerBar, ID_TMR_PAUSE, L"暂停", hInst);
    make_btn(app->hTimerBar, ID_TMR_RESET, L"重置", hInst);

    /* ---- 待办面板 ---- */
    app->hTodoPanel = CreateWindowW(TODO_PANEL_CLASS, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 10, 10, hwnd, NULL, hInst, NULL);
    SetWindowLongPtrW(app->hTodoPanel, GWLP_USERDATA, (LONG_PTR)app);

    app->hTodoTitle   = make_edit(app->hTodoPanel, ID_TODO_TITLE, hInst, 0);
    app->hTodoContent = make_edit(app->hTodoPanel, ID_TODO_CONTENT, hInst, 0);
    app->hTodoSearch  = make_edit(app->hTodoPanel, ID_TODO_SEARCH, hInst, 0);
    SendMessageW(app->hTodoTitle, EM_SETLIMITTEXT, UI_LIMIT_CHARS(TODO_TITLE_MAX), 0);
    SendMessageW(app->hTodoContent, EM_SETLIMITTEXT, UI_LIMIT_CHARS(TODO_CONTENT_MAX), 0);
    make_btn(app->hTodoPanel, ID_TODO_ADD, L"添加", hInst);
    make_btn(app->hTodoPanel, ID_TODO_REFRESH, L"刷新", hInst);

    /* 优先级改成下拉框：四象限的名字（「重要且紧急」5 个字）塞不进分段按钮，
     * 四个 88px 的按钮会把这一行挤爆。下拉框带色点，和块头颜色一致。 */
    app->hTodoPrioCombo = Ui_ComboCreate(app->hTodoPanel, ID_TODO_PRIO_COMBO, hInst, NULL, 0);
    if (app->hTodoPrioCombo) {
        for (int qi = 0; qi < TODO_QUAD_COUNT; qi++) {
            Ui_ComboAddItem(app->hTodoPrioCombo, kQuadNameU8[qi]);
            Ui_ComboSetItemDot(app->hTodoPrioCombo, qi, kQuadCat[qi]);
        }
        Ui_ComboSetSel(app->hTodoPrioCombo, 1);       /* 默认「重要不紧急」 */
    }
    app->todo_prio = TODO_PRIO_IMPORTANT;

    /* ---- 记账面板 ---- */
    app->hAcctPanel = CreateWindowW(TODO_PANEL_CLASS, NULL,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 10, 10, hwnd, NULL, hInst, NULL);
    SetWindowLongPtrW(app->hAcctPanel, GWLP_USERDATA, (LONG_PTR)app);

    app->hAcctAmount = make_edit(app->hAcctPanel, ID_ACCT_AMOUNT, hInst, 0);
    Ui_EditSetHint(app->hAcctAmount, L"0.00");
    Ui_AmountEditGuard(app->hAcctAmount);
    app->hAcctNote   = make_edit(app->hAcctPanel, ID_ACCT_NOTE, hInst, 0);
    app->hAcctSearch = make_edit(app->hAcctPanel, ID_ACCT_SEARCH, hInst, 0);

    /* 分类：自绘下拉框。
     * 原来是「只读 Edit + 单击弹菜单」，弹出层是独立窗口 + 嵌套消息循环，
     * 点击一旦穿透就会反复弹，屏幕上叠出好几层（老板反馈的 bug）。 */
    app->hAcctCatCombo = Ui_ComboCreate(app->hAcctPanel, ID_ACCT_CAT_COMBO, hInst,
                                        kStdCategories, ACCT_STD_CAT_N);
    Ui_ComboSetColorize(app->hAcctCatCombo, 1);   /* 分类色点，和列表 chip 同一套色 */
    Ui_ComboSetSel(app->hAcctCatCombo, 0);
    SendMessageW(app->hAcctNote, EM_SETLIMITTEXT, UI_LIMIT_CHARS(ACCT_NOTE_MAX), 0);

    /* 日期：自绘文本框（原生 DateTimePicker 同样无法深色化），单击弹自绘日历 */
    app->hAcctDate = make_edit(app->hAcctPanel, ID_ACCT_DATE, hInst, ES_CENTER);
    Ui_EditSetHint(app->hAcctDate, L"YYYY/MM/DD");
    SetWindowSubclass(app->hAcctDate, date_edit_subproc, 2, (DWORD_PTR)app);
    {
        char today[DATE_STR_LEN];
        WCHAR wt[DATE_STR_LEN * 2];
        get_today(today, sizeof(today));
        utf8_to_wchar(today, wt, DATE_STR_LEN * 2);
        SetWindowTextW(app->hAcctDate, wt);
    }

    make_btn(app->hAcctPanel, ID_ACCT_ADD, L"添加", hInst);
    /* 分类统计与月度报表内容重叠，合并成一个入口 */
    make_btn(app->hAcctPanel, ID_ACCT_MONTHLY, L"统计报表", hInst);
    make_btn(app->hAcctPanel, ID_ACCT_REFRESH, L"刷新", hInst);
    app->hAcctList = make_list(app->hAcctPanel, ID_ACCT_LIST, hInst);

    add_col(app->hAcctList, 0, 94,  LVCFMT_LEFT);
    add_col(app->hAcctList, 1, 84,  LVCFMT_LEFT);
    add_col(app->hAcctList, 2, 92,  LVCFMT_RIGHT);
    add_col(app->hAcctList, 3, 240, LVCFMT_LEFT);

    /* 记账列表的「分类」列画成色块、「备注」列自己排版（列号 +1，0 表示不启用） */
    SetPropW(app->hAcctList, L"chipCol", (HANDLE)(INT_PTR)2);
    SetPropW(app->hAcctList, L"noteCol", (HANDLE)(INT_PTR)4);

    Ui_EditSetHint(app->hTodoSearch, L"搜索事项");
    Ui_EditSetHint(app->hAcctSearch, L"搜索记录");
}

/* ---------------- 布局 ---------------- */

/* 编辑卡片的三条基准线，Layout 与 Paint 共用，保证绘制与控件严格对齐。
 * 记账页顶上多一张「本月支出」汇总卡，所以卡片起点按页面区分。 */
static int Ui_CardTop(void)     { return Ui_S(12); }
static int Ui_SumBottom(void)   { return Ui_CardTop() + Ui_S(UI_SUM_H); }
static int Ui_AcctCardTop(void) { return Ui_SumBottom() + Ui_S(12); }
static int Ui_Row1(int cardtop) { return cardtop + Ui_S(12); }
static int Ui_Row2(int cardtop) { return Ui_Row1(cardtop) + Ui_S(UI_INPUT_H) + Ui_S(8); }
static int Ui_CardBottom(int cardtop) { return Ui_Row2(cardtop) + Ui_S(UI_BTN_H) + Ui_S(12); }
static int Ui_HeaderY(int cardbottom) { return cardbottom + Ui_S(10); }
static int Ui_ListY(int headery)      { return headery + Ui_S(26) + Ui_S(2); }

void Ui_Layout(AppState* app, HWND hwnd, int cw, int ch) {
    const int PAD  = Ui_S(UI_PAD);
    const int TBH  = Ui_S(UI_TOPBAR_H);
    const int TBRH = Ui_S(UI_TIMERBAR_H);
    const int GAP  = Ui_S(UI_BOTTOM_GAP);
    const int IN_H = Ui_S(UI_INPUT_H);
    const int LAB  = Ui_S(UI_LABEL_W);
    const int ISX  = Ui_S(UI_INPUT_INSET_X);
    const int ISY  = Ui_S(UI_INPUT_INSET_Y);
    const int BH   = Ui_S(UI_BTN_H);

    int bar_y = ch - TBRH - GAP;
    if (bar_y < TBH + Ui_S(80)) bar_y = TBH + Ui_S(80);

    app->rt_brand.left = Ui_S(20);
    app->rt_brand.right = app->rt_brand.left + Ui_S(64);
    app->rt_brand.top = 0;
    app->rt_brand.bottom = TBH;

    int fw = Ui_S(62), fh = Ui_S(28);
    app->rt_file.left = Ui_S(92);
    app->rt_file.right = app->rt_file.left + fw;
    app->rt_file.top = (TBH - fh) / 2;
    app->rt_file.bottom = app->rt_file.top + fh;

    /* 右上角窗口按钮：最小化 / 最大化 / 关闭，紧贴右边缘（自绘标题栏） */
    {
        int wbw = Ui_S(46);
        app->rt_close.left = cw - wbw;
        app->rt_close.right = cw;
        app->rt_max.left = cw - wbw * 2;
        app->rt_max.right = cw - wbw;
        app->rt_min.left = cw - wbw * 3;
        app->rt_min.right = cw - wbw * 2;
        app->rt_close.top = app->rt_max.top = app->rt_min.top = 0;
        app->rt_close.bottom = app->rt_max.bottom = app->rt_min.bottom = TBH;
    }

    int tw = Ui_S(30);
    app->rt_theme.right = app->rt_min.left - Ui_S(12);
    app->rt_theme.left = app->rt_theme.right - tw;
    app->rt_theme.top = (TBH - tw) / 2;
    app->rt_theme.bottom = app->rt_theme.top + tw;

    int tabw = Ui_S(82), tabg = Ui_S(4), tabh = Ui_S(30);
    int tabs_right = app->rt_theme.left - Ui_S(12);
    int tabs_left = tabs_right - (tabw * 2 + tabg);
    app->rt_tab0.left = tabs_left;
    app->rt_tab0.right = tabs_left + tabw;
    app->rt_tab0.top = (TBH - tabh) / 2;
    app->rt_tab0.bottom = app->rt_tab0.top + tabh;
    app->rt_tab1.left = tabs_left + tabw + tabg;
    app->rt_tab1.right = app->rt_tab1.left + tabw;
    app->rt_tab1.top = app->rt_tab0.top;
    app->rt_tab1.bottom = app->rt_tab0.bottom;

    /* 「文件」和页签之间的天气位：紧贴文件按钮右侧正向摆放，宽度按文字实测。
       放得下「城市 气象 温度」就用全串（多宽都行，只要不压到页签/时钟），
       放不下退成「城市 温度」；还放不下才整块置空。
       时钟右对齐到页签左侧，跟天气抢地方时优先保天气（时钟本来就只在宽窗口下出现）。 */
    {
        int wh = Ui_S(24);
        const int wleft = app->rt_file.right + Ui_S(16);
        app->rt_weather.left = wleft;
        app->rt_weather.top  = (TBH - wh) / 2;
        app->rt_weather.bottom = app->rt_weather.top + wh;

        app->rt_clock.right = app->rt_tab0.left - Ui_S(24);
        app->rt_clock.left  = app->rt_clock.right - Ui_S(190);
        app->rt_clock.top   = app->rt_weather.top;
        app->rt_clock.bottom = app->rt_weather.bottom;

        char full[96] = {0}, brief[96] = {0};
        WCHAR wfull[128] = {0}, wbrief[128] = {0};
        if (!Weather_Text(full, sizeof(full))) {
            /* 还没拿到数据：占位串，宽度也按它算，避免数据回来后标题栏跳一下 */
            _snprintf(full, sizeof(full), "%s --°C", Weather_City());
            full[sizeof(full) - 1] = 0;
        }
        Weather_Brief(brief, sizeof(brief));
        if (!brief[0]) {
            _snprintf(brief, sizeof(brief), "%s --°C", Weather_City());
            brief[sizeof(brief) - 1] = 0;
        }
        utf8_to_wchar(full, wfull, 128);
        utf8_to_wchar(brief, wbrief, 128);

        HDC dc = GetDC(hwnd);
        int twf = Ui_TextWidth(dc, wfull, Ui_Font(UI_FONT_SMALL));
        int twb = Ui_TextWidth(dc, wbrief, Ui_Font(UI_FONT_SMALL));
        ReleaseDC(hwnd, dc);

        int room = app->rt_tab0.left - Ui_S(12) - wleft;   /* 到页签的可用宽度 */
        int ww = twf + Ui_S(4);
        app->weather_compact = 0;
        if (ww > room) {          /* 全串放不下 → 退成简版 */
            ww = twb + Ui_S(4);
            app->weather_compact = 1;
        }
        app->rt_weather.right = wleft + ww;

        /* 时钟压到天气就整个不画 */
        if (app->rt_clock.left < app->rt_weather.right + Ui_S(12))
            app->rt_clock.left = app->rt_clock.right = 0;
        /* 简版都放不下 → 天气整块收掉 */
        if (app->rt_weather.right > app->rt_tab0.left - Ui_S(12)) {
            app->rt_weather.left = app->rt_weather.right = 0;
            app->rt_weather.top  = app->rt_weather.bottom = 0;
        }
    }

    SetWindowPos(app->hTimerBar, NULL, PAD, bar_y, cw - PAD * 2, TBRH, SWP_NOZORDER);

    {
        int ti = Ui_S(UI_TIMER_IN);
        int ey = (TBRH - ti) / 2;
        int ex = Ui_S(56);
        int ew = Ui_S(30);
        int ins = Ui_S(UI_TIGHT_INSET);
        /* 节距 = 槽宽(ew+2*ins) + 空隙 + 单位字宽，留够不打架的余量 */
        int step = Ui_S(66);
        SetWindowPos(app->hTimerHour, NULL, ex + ins, ey + ins, ew, ti - ins * 2, SWP_NOZORDER);
        SetWindowPos(app->hTimerMin, NULL, ex + step + ins, ey + ins, ew, ti - ins * 2, SWP_NOZORDER);
        SetWindowPos(app->hTimerSec, NULL, ex + step * 2 + ins, ey + ins, ew, ti - ins * 2, SWP_NOZORDER);

        int bx = Ui_S(256);
        int bw = Ui_S(48), bh = BH;
        int by = (TBRH - bh) / 2;
        SetWindowPos(GetDlgItem(app->hTimerBar, ID_TMR_START), NULL, bx, by, bw, bh, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(app->hTimerBar, ID_TMR_PAUSE), NULL, bx + Ui_S(54), by, bw, bh, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(app->hTimerBar, ID_TMR_RESET), NULL, bx + Ui_S(108), by, bw, bh, SWP_NOZORDER);
    }

    int panel_y = TBH;
    int panel_h = bar_y - TBH;
    if (panel_h < Ui_S(60)) panel_h = Ui_S(60);
    SetWindowPos(app->hTodoPanel, NULL, 0, panel_y, cw, panel_h, SWP_NOZORDER);
    SetWindowPos(app->hAcctPanel, NULL, 0, panel_y, cw, panel_h, SWP_NOZORDER);

    int inner_l = PAD + Ui_S(12);
    int inner_r = cw - PAD - Ui_S(12);
    int gap = Ui_S(10);
    int y1 = Ui_Row1(Ui_CardTop());
    int y2 = Ui_Row2(Ui_CardTop());
    int list_y = Ui_ListY(Ui_HeaderY(Ui_CardBottom(Ui_CardTop())));
    int list_h = panel_h - list_y - Ui_S(12);
    if (list_h < Ui_S(40)) list_h = Ui_S(40);
    /* 记账页的卡片整体下移，让出顶上那张汇总卡 */
    int ay1 = Ui_Row1(Ui_AcctCardTop());
    int ay2 = Ui_Row2(Ui_AcctCardTop());
    int alist_y = Ui_ListY(Ui_HeaderY(Ui_CardBottom(Ui_AcctCardTop())));
    int alist_h = panel_h - alist_y - Ui_S(12);
    if (alist_h < Ui_S(40)) alist_h = Ui_S(40);
    int listw = cw - PAD * 2;
    (void)LAB;

    {
        int edit_w = (inner_r - inner_l - LAB * 2 - gap * 3) / 2;
        if (edit_w < Ui_S(90)) edit_w = Ui_S(90);
        SetWindowPos(app->hTodoTitle, NULL, inner_l + LAB + gap + ISX, y1 + ISY,
                     edit_w - ISX * 2, IN_H - ISY * 2, SWP_NOZORDER);
        int x2 = inner_l + LAB + gap + edit_w + gap + LAB + gap;
        SetWindowPos(app->hTodoContent, NULL, x2 + ISX, y1 + ISY,
                     edit_w - ISX * 2, IN_H - ISY * 2, SWP_NOZORDER);

        /* 按钮行：左侧「优先级」下拉框，后面跟 添加 / 搜索 / 刷新。
           这一行从 inner_l 起排（不像输入行那样跳过标签列），标签就占最左边一列，
           与上方「标题」「内容」两个标签左对齐。 */
        int bx0 = inner_l;
        int cx0 = bx0 + LAB + gap;
        int pw  = Ui_S(126);
        if (app->hTodoPrioCombo)
            SetWindowPos(app->hTodoPrioCombo, NULL, cx0, y2, pw, BH, SWP_NOZORDER);
        int nx = cx0 + pw + Ui_S(10);
        SetWindowPos(GetDlgItem(app->hTodoPanel, ID_TODO_ADD), NULL,
                     nx, y2, Ui_S(64), BH, SWP_NOZORDER);
        nx += Ui_S(64) + Ui_S(10);
        SetWindowPos(app->hTodoSearch, NULL, nx + ISX, y2 + ISY,
                     Ui_S(150), IN_H - ISY * 2, SWP_NOZORDER);
        nx += Ui_S(150) + Ui_S(12);
        SetWindowPos(GetDlgItem(app->hTodoPanel, ID_TODO_REFRESH), NULL,
                     nx, y2, Ui_S(60), BH, SWP_NOZORDER);

        /* 四象限：卡片下方到面板底部，2 列 × 2 行 */
        {
            int qx = PAD, qy = list_y;
            int qw = (listw - gap) / 2;
            int qh = (list_h - gap) / 2;
            if (qh < Ui_S(84)) qh = Ui_S(84);
            for (int q = 0; q < TODO_QUAD_COUNT; q++) {
                int col = q % 2, rown = q / 2;
                app->rt_quad[q].left   = qx + col * (qw + gap);
                app->rt_quad[q].top    = qy + rown * (qh + gap);
                app->rt_quad[q].right  = app->rt_quad[q].left + qw;
                app->rt_quad[q].bottom = app->rt_quad[q].top + qh;
            }
            int inner = qh - Ui_S(QUAD_HEAD_H) - Ui_S(6);
            int rv = inner / Ui_S(QUAD_ROW_H);
            if (rv < 1) rv = 1;
            app->quad_rows_vis = rv;
        }
    }

    {
        int x = inner_l;
        int aw = Ui_S(88), cwid = Ui_S(104), dw = Ui_S(116);
        SetWindowPos(app->hAcctAmount, NULL, x + LAB + gap + ISX, ay1 + ISY,
                     aw - ISX * 2, IN_H - ISY * 2, SWP_NOZORDER);
        x += LAB + gap + aw + gap;
        /* 下拉框自己画边框，所以占满整个槽位，不再内缩 */
        SetWindowPos(app->hAcctCatCombo, NULL, x + LAB + gap, ay1,
                     cwid, IN_H, SWP_NOZORDER);
        x += LAB + gap + cwid + gap;
        SetWindowPos(app->hAcctDate, NULL, x + LAB + gap + ISX, ay1 + ISY,
                     dw - ISX * 2 - Ui_S(UI_DATE_CAL_PAD), IN_H - ISY * 2, SWP_NOZORDER);
        x += LAB + gap + dw + gap;
        int nw = inner_r - (x + LAB + gap);
        if (nw < Ui_S(80)) nw = Ui_S(80);
        SetWindowPos(app->hAcctNote, NULL, x + LAB + gap + ISX, ay1 + ISY,
                     nw - ISX * 2, IN_H - ISY * 2, SWP_NOZORDER);

        int bx2 = inner_l + LAB + gap;
        SetWindowPos(GetDlgItem(app->hAcctPanel, ID_ACCT_ADD), NULL, bx2, ay2, Ui_S(60), BH, SWP_NOZORDER);
        bx2 += Ui_S(68);
        SetWindowPos(GetDlgItem(app->hAcctPanel, ID_ACCT_MONTHLY), NULL, bx2, ay2, Ui_S(84), BH, SWP_NOZORDER);
        bx2 += Ui_S(92);
        SetWindowPos(app->hAcctSearch, NULL, bx2 + ISX, ay2 + ISY, Ui_S(150), IN_H - ISY * 2, SWP_NOZORDER);
        bx2 += Ui_S(170);
        SetWindowPos(GetDlgItem(app->hAcctPanel, ID_ACCT_REFRESH), NULL, bx2, ay2, Ui_S(56), BH, SWP_NOZORDER);
        SetWindowPos(app->hAcctList, NULL, PAD, alist_y, listw, alist_h, SWP_NOZORDER);
    }

    Ui_ScrollAttach(app->hAcctList);

    {
        int ac[4];
        int fitw = list_fit_width(listw);
        calc_acct_cols(fitw, ac);
        for (int i = 0; i < 4; i++)
            SendMessageW(app->hAcctList, LVM_SETCOLUMNWIDTH, i, ac[i]);
    }
}

/* ---------------- 绘制辅助 ---------------- */


static void edit_slot(HWND hEdit, RECT* slot) {
    if (!hEdit) { memset(slot, 0, sizeof(*slot)); return; }
    int ix = Ui_S(UI_INPUT_INSET_X);
    int iy = Ui_S(UI_INPUT_INSET_Y);
    RECT r;
    GetWindowRect(hEdit, &r);
    POINT p = { r.left, r.top };
    ScreenToClient(GetParent(hEdit), &p);
    slot->left = p.x - ix;
    slot->top = p.y - iy;
    slot->right = p.x + (r.right - r.left) + ix;
    slot->bottom = p.y + (r.bottom - r.top) + iy;
}

/* 倒计时那种窄框专用的紧凑槽（inset 见文件头的 UI_TIGHT_INSET） */
static void edit_slot_tight(HWND hEdit, RECT* slot) {
    if (!hEdit) { memset(slot, 0, sizeof(*slot)); return; }
    int ix = Ui_S(UI_TIGHT_INSET);
    RECT r;
    GetWindowRect(hEdit, &r);
    POINT p = { r.left, r.top };
    ScreenToClient(GetParent(hEdit), &p);
    slot->left = p.x - ix;
    slot->top = p.y - ix;
    slot->right = p.x + (r.right - r.left) + ix;
    slot->bottom = p.y + (r.bottom - r.top) + ix;
}

static void draw_slot_tight(HDC hdc, HWND hEdit) {
    RECT slot;
    edit_slot_tight(hEdit, &slot);
    if (slot.right <= slot.left) return;
    const Theme* t = Theme_Now();
    Ui_FillRound(hdc, &slot, Ui_S(5), t->input_bg);
    Ui_StrokeRound(hdc, &slot, Ui_S(5), t->input_border, 1.0f);
}

static void draw_slot_only(HDC hdc, HWND hEdit) {
    RECT slot;
    edit_slot(hEdit, &slot);
    if (slot.right <= slot.left) return;
    const Theme* t = Theme_Now();
    Ui_FillRound(hdc, &slot, Ui_S(6), t->input_bg);
    Ui_StrokeRound(hdc, &slot, Ui_S(6), t->input_border, 1.0f);
}

/* 只画字段标签，不画槽 —— 给「自己画边框」的控件（自绘下拉框）用 */
static void draw_field_label(HDC hdc, HWND hCtl, const WCHAR* label) {
    if (!hCtl) return;
    RECT r;
    GetWindowRect(hCtl, &r);
    POINT p = { r.left, r.top };
    ScreenToClient(GetParent(hCtl), &p);
    RECT lr;
    lr.right  = p.x - Ui_S(8);
    lr.left   = lr.right - Ui_S(UI_LABEL_W);
    lr.top    = p.y;
    lr.bottom = p.y + (r.bottom - r.top);
    if (lr.right <= lr.left) return;
    Ui_Text(hdc, label, &lr, Theme_Now()->text_sub, Ui_Font(UI_FONT_SMALL),
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}


static void draw_slot_label(HDC hdc, HWND hEdit, const WCHAR* label) {
    RECT slot;
    edit_slot(hEdit, &slot);
    if (slot.right <= slot.left) return;
    const Theme* t = Theme_Now();
    Ui_FillRound(hdc, &slot, Ui_S(6), t->input_bg);
    Ui_StrokeRound(hdc, &slot, Ui_S(6), t->input_border, 1.0f);

    RECT lr;
    lr.right = slot.left - Ui_S(8);
    lr.left = lr.right - Ui_S(UI_LABEL_W);
    lr.top = slot.top;
    lr.bottom = slot.bottom;
    Ui_Text(hdc, label, &lr, t->text_sub, Ui_Font(UI_FONT_SMALL),
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

}


/* 日期框：右侧留出一个图标位，输入框本身缩窄，避免子窗口把图标盖住 */
static void draw_date_field(HDC hdc, HWND hEdit) {
    RECT slot;
    edit_slot(hEdit, &slot);
    if (slot.right <= slot.left) return;
    const Theme* t = Theme_Now();
    int pad = Ui_S(UI_DATE_CAL_PAD);
    slot.right += pad;

    Ui_FillRound(hdc, &slot, Ui_S(6), t->input_bg);
    Ui_StrokeRound(hdc, &slot, Ui_S(6), t->input_border, 1.0f);

    RECT lr;
    lr.right = slot.left - Ui_S(8);
    lr.left = lr.right - Ui_S(UI_LABEL_W);
    lr.top = slot.top;
    lr.bottom = slot.bottom;
    Ui_Text(hdc, L"日期", &lr, t->text_sub, Ui_Font(UI_FONT_SMALL),
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* 小日历图标 */
    int s  = Ui_S(13);
    int cx = slot.right - Ui_S(10) - s / 2;
    int cy = (slot.top + slot.bottom) / 2;
    Ui_DrawCalGlyph(hdc, cx, cy, s, t->text_dim);
}

static void draw_header_row(HDC hdc, int x0, int y, int w, const int cols[4],
                            const WCHAR* const names[4], const int right_align[4]) {
    const Theme* t = Theme_Now();
    int h = Ui_S(26);
    RECT hr = { x0, y, x0 + w, y + h };
    Ui_FillSolid(hdc, &hr, t->bg);

    int x = x0;
    for (int i = 0; i < 4; i++) {
        if (names[i]) {
            RECT r = { x + Ui_S(14), y, x + cols[i] - Ui_S(14), y + h };
            if (right_align && right_align[i]) {
                Ui_Text(hdc, names[i], &r, t->text_dim, Ui_Font(UI_FONT_SMALL),
                        DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            } else {
                Ui_Text(hdc, names[i], &r, t->text_dim, Ui_Font(UI_FONT_SMALL),
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        x += cols[i];
    }
}

/* ---------------- 主窗口绘制 ---------------- */

static void draw_tab(HDC hdc, const RECT* rt, const WCHAR* text, int active, int hovered) {
    const Theme* t = Theme_Now();
    RECT r = *rt;
    if (active) {
        Ui_FillRound(hdc, &r, Ui_S(7), t->accent_soft);
        Ui_Text(hdc, text, &r, t->accent_soft_fg, Ui_Font(UI_FONT_LABEL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        if (hovered) Ui_FillRound(hdc, &r, Ui_S(7), t->surface_alt);
        Ui_Text(hdc, text, &r, hovered ? t->text : t->text_sub, Ui_Font(UI_FONT_LABEL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}


/* 窗口控制按钮：kind 0=最小化 1=最大化 2=关闭 3=还原 */
static void draw_cap_btn(HDC hdc, const RECT* rt, int kind, int state) {
    const Theme* t = Theme_Now();
    RECT r = *rt;
    COLORREF fg = t->text_sub;

    if (state != UI_ST_NORMAL) {
        RECT h = r;
        InflateRect(&h, -Ui_S(3), -Ui_S(3));
        if (kind == 2) {
            Ui_FillRound(hdc, &h, Ui_S(5), t->danger);
            fg = RGB(255, 255, 255);
        } else {
            Ui_FillRound(hdc, &h, Ui_S(5), t->hover_bg);
            fg = t->text;
        }
    }

    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;
    int s  = Ui_S(5);

    HPEN pen = CreatePen(PS_SOLID, 1, fg);
    HPEN  op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    switch (kind) {
    case 0:  /* — */
        MoveToEx(hdc, cx - s, cy, NULL);
        LineTo(hdc, cx + s + 1, cy);
        break;
    case 1:  /* □ */
        Rectangle(hdc, cx - s + 1, cy - s + 1, cx + s, cy + s);
        break;
    case 2:  /* ✕ */
        MoveToEx(hdc, cx - s, cy - s, NULL);
        LineTo(hdc, cx + s + 1, cy + s + 1);
        MoveToEx(hdc, cx + s, cy - s, NULL);
        LineTo(hdc, cx - s - 1, cy + s + 1);
        break;
    default: /* ❐ 还原 */
        Rectangle(hdc, cx - s + 1, cy - s + 3, cx + s - 1, cy + s);
        MoveToEx(hdc, cx - s + 3, cy - s + 1, NULL);
        LineTo(hdc, cx + s, cy - s + 1);
        LineTo(hdc, cx + s, cy + s - 3);
        break;
    }

    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pen);
}

void Ui_PaintMain(AppState* app, HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const Theme* t = Theme_Now();

    Ui_FillSolid(hdc, &rc, t->bg);

    int topH = Ui_S(UI_TOPBAR_H);
    RECT top = { 0, 0, rc.right, topH };
    Ui_FillSolid(hdc, &top, t->surface);

    RECT line = { 0, topH - 1, rc.right, topH };
    Ui_FillSolid(hdc, &line, t->border_soft);

    {
        RECT br = app->rt_brand;
        br.left += Ui_S(2);
        Ui_Text(hdc, L"TodoApp", &br, t->text, Ui_Font(UI_FONT_BRAND),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    {
        /* 文件菜单：无边框 ghost 按钮，文字左对齐，右侧留出箭头位置 */
        int st = UI_ST_NORMAL;
        if (app->pressed_hit == HIT_FILE) st = UI_ST_PRESSED;
        else if (app->hover_hit == HIT_FILE) st = UI_ST_HOVER;

        RECT r = app->rt_file;
        if (st != UI_ST_NORMAL)
            Ui_FillRound(hdc, &r, Ui_S(6), t->accent_soft);

        RECT tr = r;
        tr.left += Ui_S(12);
        tr.right -= Ui_S(16);
        Ui_Text(hdc, L"文件", &tr,
                st == UI_ST_NORMAL ? t->text_sub : t->accent_soft_fg,
                Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        HPEN pen = CreatePen(PS_SOLID, 1,
                             st == UI_ST_NORMAL ? t->text_dim : t->accent_soft_fg);
        HBRUSH br = CreateSolidBrush(st == UI_ST_NORMAL ? t->text_dim : t->accent_soft_fg);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        int cx = app->rt_file.right - Ui_S(11);
        int cy = (app->rt_file.top + app->rt_file.bottom) / 2 + Ui_S(1);
        int s = Ui_S(3);
        POINT tri[3] = { { cx - s, cy - s / 2 }, { cx + s, cy - s / 2 }, { cx, cy + s } };
        Polygon(hdc, tri, 3);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pen);
        DeleteObject(br);
    }

    /* 城市 + 气象 + 气温：双击可改城市。还没拿到数据时显示「城市 --°C」，位置不跳 */
    if (app->rt_weather.right > app->rt_weather.left) {
        char b8[96];
        int ok = app->weather_compact ? Weather_Brief(b8, sizeof(b8))
                                      : Weather_Text(b8, sizeof(b8));
        if (!ok) {
            _snprintf(b8, sizeof(b8), "%s --°C", Weather_City());
            b8[sizeof(b8) - 1] = 0;
        }
        WCHAR wb[128];
        utf8_to_wchar(b8, wb, 128);
        RECT wr = app->rt_weather;
        if (app->hover_hit == HIT_WEATHER) {
            InflateRect(&wr, Ui_S(6), Ui_S(3));
            Ui_FillRound(hdc, &wr, Ui_S(5), t->accent_soft);
            wr = app->rt_weather;
        }
        Ui_Text(hdc, wb, &wr,
                app->hover_hit == HIT_WEATHER ? t->accent_soft_fg : t->text_sub,
                Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (app->rt_clock.right > app->rt_clock.left) {
        RECT cr = app->rt_clock;
        Ui_Text(hdc, app->clock_text, &cr, t->text_dim, Ui_Font(UI_FONT_SMALL),
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    draw_tab(hdc, &app->rt_tab0, L"待办事项", app->current_tab == 0,
             app->hover_hit == HIT_TAB0);
    draw_tab(hdc, &app->rt_tab1, L"记账本", app->current_tab == 1,
             app->hover_hit == HIT_TAB1);

    {
        RECT tr = app->rt_theme;
        if (app->hover_hit == HIT_THEME) {
            RECT h = tr;
            InflateRect(&h, -Ui_S(2), -Ui_S(4));
            Ui_FillRound(hdc, &h, Ui_S(6), t->accent_soft);
        }
        Ui_DrawIcon(hdc, &tr, Theme_IsDark() ? 0 : 1, t->text_sub);
    }

    /* 窗口按钮：最小化 / 最大化(还原) / 关闭 */
    {
        int zoomed = IsZoomed(hwnd);
        int st;

        st = UI_ST_NORMAL;
        if (app->pressed_hit == HIT_MIN) st = UI_ST_PRESSED;
        else if (app->hover_hit == HIT_MIN) st = UI_ST_HOVER;
        draw_cap_btn(hdc, &app->rt_min, 0, st);

        st = UI_ST_NORMAL;
        if (app->pressed_hit == HIT_MAX) st = UI_ST_PRESSED;
        else if (app->hover_hit == HIT_MAX) st = UI_ST_HOVER;
        draw_cap_btn(hdc, &app->rt_max, zoomed ? 3 : 1, st);

        st = UI_ST_NORMAL;
        if (app->pressed_hit == HIT_CLOSE) st = UI_ST_PRESSED;
        else if (app->hover_hit == HIT_CLOSE) st = UI_ST_HOVER;
        draw_cap_btn(hdc, &app->rt_close, 2, st);
    }

    /* 外框：无系统边框后靠 1px 线界定窗口轮廓 */
    {
        RECT e;
        e.left = 0; e.right = rc.right; e.top = 0; e.bottom = 1;
        Ui_FillSolid(hdc, &e, t->border);
        e.top = rc.bottom - 1; e.bottom = rc.bottom;
        Ui_FillSolid(hdc, &e, t->border);
        e.top = 0; e.bottom = rc.bottom; e.left = 0; e.right = 1;
        Ui_FillSolid(hdc, &e, t->border);
        e.left = rc.right - 1; e.right = rc.right;
        Ui_FillSolid(hdc, &e, t->border);
    }
}

/* ---------------- 面板绘制 ---------------- */

/* 汇总卡的数字全部按 app->acct_month 算，跟列表显示的月份保持一致。
 * 支出与收入分开算：卡片上要同时给出结余 */
static void acct_month_sum(AppState* app, const char* month,
                           double* expense, double* income, int* n) {
    *expense = 0;
    *income  = 0;
    *n = 0;
    if (!month || !month[0]) return;
    for (int i = 0; i < app->accounts.count; i++) {
        const AccountRecord* r = &app->accounts.items[i];
        if (r->deleted) continue;
        char im[16];
        get_month_from_date(r->date, im, sizeof(im));
        if (strcmp(im, month) != 0) continue;
        if (acct_is_income(r)) *income += r->amount;
        else                   *expense += r->amount;
        (*n)++;
    }
}

/* 当前筛选的月份；空则回落到本月 */
static void acct_month_cur(AppState* app, char* month, int cap) {
    if (app->acct_month[0]) {
        strncpy(month, app->acct_month, (size_t)cap - 1);
        month[cap - 1] = 0;
        return;
    }
    char today[DATE_STR_LEN];
    month[0] = 0;
    get_today(today, sizeof(today));
    get_month_from_date(today, month, cap);
}

/* "YYYY-MM" → 上一个月 */
static void prev_month(const char* cur, char* out, int cap) {
    int y = 0, m = 0;
    if (sscanf(cur, "%d-%d", &y, &m) != 2) { out[0] = 0; return; }
    m--;
    if (m < 1) { m = 12; y--; }
    _snprintf(out, (size_t)cap, "%04d-%02d", y, m);
    out[cap - 1] = 0;
}

/* 金额格式化：三位一逗号（mockup 里是 ¥3,842.10 这种写法） */
static void fmt_money(WCHAR* out, int cap, double v) {
    WCHAR tmp[64];
    _snwprintf(tmp, 64, L"%.2f", v < 0 ? -v : v);
    tmp[63] = 0;

    WCHAR* dot = wcschr(tmp, L'.');
    int ilen = dot ? (int)(dot - tmp) : (int)wcslen(tmp);
    WCHAR res[96];
    int r = 0;
    if (v < 0 && r < 95) res[r++] = L'-';
    if (r < 95) res[r++] = L'\xA5';          /* ¥ */
    for (int i = 0; i < ilen && r < 94; i++) {
        res[r++] = tmp[i];
        int left = ilen - 1 - i;
        if (left > 0 && left % 3 == 0) res[r++] = L',';
    }
    if (dot && r < 93) {
        res[r++] = L'.';
        res[r++] = dot[1] ? dot[1] : L'0';
        res[r++] = dot[2] ? dot[2] : L'0';
    }
    res[r] = 0;

    wcsncpy(out, res, (size_t)cap - 1);
    out[cap - 1] = 0;
}

/* 汇总卡右上角的月份切换按钮（点击热区），供绘制与命中测试共用 */
RECT Ui_SumMonthRect(HWND panel) {
    RECT rc = { 0, 0, 0, 0 };
    RECT out = { 0, 0, 0, 0 };
    if (!panel) return out;
    GetClientRect(panel, &rc);
    int PAD = Ui_S(UI_PAD);
    int top = Ui_CardTop();
    out.right = rc.right - PAD - Ui_S(14);
    out.left = out.right - Ui_S(106);
    out.top = top + Ui_S(11);
    out.bottom = out.top + Ui_S(24);
    return out;
}

/* 记账页「支出 / 收入」分段按钮的矩形（面板坐标） */
RECT Ui_AcctTypeRect(HWND panel) {
    RECT rc = { 0, 0, 0, 0 };
    RECT out = { 0, 0, 0, 0 };
    if (!panel) return out;
    GetClientRect(panel, &rc);
    int PAD = Ui_S(UI_PAD);
    int w = Ui_S(116), h = Ui_S(UI_BTN_H);
    out.right  = rc.right - PAD;
    out.left   = out.right - w;
    out.top    = Ui_Row2(Ui_AcctCardTop());
    out.bottom = out.top + h;
    return out;
}

static void draw_acct_type_seg(AppState* app, HDC hdc) {
    const Theme* t = Theme_Now();
    RECT seg = Ui_AcctTypeRect(app->hAcctPanel);
    if (seg.right <= seg.left) return;

    Ui_FillRound(hdc, &seg, Ui_S(7), t->input_bg);
    Ui_StrokeRound(hdc, &seg, Ui_S(7), t->input_border, 1.0f);

    static const WCHAR* names[2] = { L"支出", L"收入" };
    int half = (seg.right - seg.left) / 2;
    for (int i = 0; i < 2; i++) {
        RECT hr = seg;
        hr.left  = seg.left + half * i;
        hr.right = (i == 0) ? (seg.left + half) : seg.right;
        int on = (app->acct_type == i);
        int hv = (app->hover_acct_type == i + 1);

        if (on || hv) {
            RECT f = hr;
            f.left += Ui_S(2); f.right -= Ui_S(2);
            f.top  += Ui_S(2); f.bottom -= Ui_S(2);
            Ui_FillRound(hdc, &f, Ui_S(5), on ? (i ? t->success : t->accent) : t->hover_bg);
        }
        Ui_Text(hdc, names[i], &hr, on ? t->accent_fg : t->text_sub,
                Ui_Font(UI_FONT_SMALL), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ---------------- 待办四象限：几何与绘制 ---------------- */

int Ui_QuadRowRect(AppState* app, int q, int row, RECT* out) {
    if (!app || !out || q < 0 || q >= TODO_QUAD_COUNT) return 0;
    int k = row - app->quad_scroll[q];          /* 减掉滚动偏移 → 屏幕上的第几行 */
    if (k < 0 || k >= app->quad_rows_vis) return 0;
    const RECT* r = &app->rt_quad[q];
    int rh = Ui_S(QUAD_ROW_H);
    out->left   = r->left + Ui_S(8);
    out->right  = r->right - Ui_S(8);
    out->top    = r->top + Ui_S(QUAD_HEAD_H) + k * rh;
    out->bottom = out->top + rh;
    return 1;
}

int Ui_QuadHit(AppState* app, POINT pt, int* row) {
    if (row) *row = -1;
    if (!app) return -1;
    for (int q = 0; q < TODO_QUAD_COUNT; q++) {
        RECT r = app->rt_quad[q];
        if (r.right <= r.left) continue;
        if (!PtInRect(&r, pt)) continue;
        if (!row) return q;
        int head = Ui_S(QUAD_HEAD_H);
        int rh   = Ui_S(QUAD_ROW_H);
        int rel  = pt.y - (r.top + head);
        if (rel >= 0) {
            int k = rel / rh;
            if (k < app->quad_rows_vis) {
                int idx = app->quad_scroll[q] + k;
                if (idx < app->quad_n[q]) *row = idx;
            }
        }
        return q;
    }
    return -1;
}

/* 标题行右侧的计数胶囊 */
static void draw_quad_count(HDC hdc, const RECT* head, int n, COLORREF fg) {
    if (n <= 0) return;
    WCHAR buf[16];
    _snwprintf(buf, 16, L"%d", n);
    buf[15] = 0;
    int tw = Ui_TextWidth(hdc, buf, Ui_Font(UI_FONT_SMALL));
    int w = tw + Ui_S(14);
    int h = Ui_S(18);
    RECT c = { head->right - w, (head->top + head->bottom) / 2 - h / 2,
               head->right, (head->top + head->bottom) / 2 + h / 2 };
    Ui_FillRound(hdc, &c, h / 2, mix_color(fg, Theme_Now()->surface, Theme_Now()->dark ? 78 : 86));
    Ui_Text(hdc, buf, &c, fg, Ui_Font(UI_FONT_SMALL),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_quad_card(AppState* app, HDC hdc, int q) {
    const Theme* t = Theme_Now();
    RECT r = app->rt_quad[q];
    if (r.right <= r.left || r.bottom <= r.top) return;

    COLORREF bg = quad_bg(q), fg = Ui_QuadColor(q);
    Ui_FillRound(hdc, &r, Ui_S(10), bg);
    Ui_StrokeRound(hdc, &r, Ui_S(10), mix_color(bg, fg, t->dark ? 26 : 20), 1.0f);

    /* 标题：色点 + 名字 + 右侧计数胶囊 */
    int pad = Ui_S(12);
    RECT head = { r.left + pad, r.top + Ui_S(6), r.right - pad, r.top + Ui_S(QUAD_HEAD_H) };
    {
        int cy = (head.top + head.bottom) / 2;
        RECT dot = { head.left, cy - Ui_S(4), head.left + Ui_S(8), cy + Ui_S(4) };
        Ui_FillRound(hdc, &dot, Ui_S(4), fg);
    }
    RECT tr = head;
    tr.left += Ui_S(16);
    draw_quad_count(hdc, &head, app->quad_total[q], fg);
    tr.right -= Ui_S(40);
    Ui_Text(hdc, kQuadName[q], &tr, fg, Ui_Font(UI_FONT_TITLE),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    /* 条目 */
    int n = app->quad_n[q];
    if (n <= 0) {
        RECT er = { r.left + pad, r.top + Ui_S(QUAD_HEAD_H), r.right - pad, r.bottom - Ui_S(6) };
        Ui_Text(hdc, L"暂无事项", &er, quad_muted(q), Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_TOP | DT_SINGLELINE);
        return;
    }

    int start = app->quad_scroll[q];
    for (int k = 0; k < app->quad_rows_vis; k++) {
        int idx = start + k;
        if (idx >= n) break;
        Todo* td = todo_list_find(&app->todos, app->quad_ids[q][idx]);
        if (!td) continue;

        RECT rr;
        if (!Ui_QuadRowRect(app, q, idx, &rr)) break;

        int hov = (app->quad_hover == q && app->quad_hover_row == idx);
        int sel = (app->quad_sel_id == td->id);
        if (sel)      Ui_FillRound(hdc, &rr, Ui_S(6), mix_color(bg, fg, t->dark ? 26 : 14));
        else if (hov) Ui_FillRound(hdc, &rr, Ui_S(6), mix_color(bg, fg, t->dark ? 16 : 8));

        /* 左：勾选圆 */
        int ccx = rr.left + Ui_S(13);
        int ccy = (rr.top + rr.bottom) / 2;
        Ui_DrawCheckMark(hdc, ccx, ccy, Ui_S(15), td->completed, t->success,
                         t->dark ? RGB(0x8A, 0x8F, 0x98) : RGB(0xA8, 0xAE, 0xB8));

        /* 中：标题（完成态压暗），右侧留出重复/提醒的位置 */
        WCHAR sub[96];
        sub[0] = 0;
        {
            char s8[96];
            todo_quad_sub(td, s8, sizeof(s8));
            if (s8[0]) utf8_to_wchar(s8, sub, 96);
        }
        int rw = 0;
        if (sub[0]) rw = Ui_TextWidth(hdc, sub, Ui_Font(UI_FONT_SMALL)) + Ui_S(10);

        RECT txt = rr;
        txt.left = ccx + Ui_S(13);
        txt.right -= rw;
        {
            WCHAR wt[TODO_TITLE_MAX];
            utf8_to_wchar(td->title, wt, TODO_TITLE_MAX);
            Ui_Text(hdc, wt, &txt, td->completed ? t->text_dim : t->text,
                    Ui_Font(UI_FONT_BODY),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        if (sub[0]) {
            RECT sr = rr;
            sr.left = rr.right - rw;
            sr.right -= Ui_S(4);
            Ui_Text(hdc, sub, &sr, t->text_sub, Ui_Font(UI_FONT_SMALL),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    /* 还有没显示的条目：右下角提示 + 一条细滚动条 */
    if (n > app->quad_rows_vis) {
        int vis = app->quad_rows_vis;
        int maxs = n - vis;
        if (app->quad_scroll[q] > maxs) app->quad_scroll[q] = maxs;
        if (app->quad_scroll[q] < 0) app->quad_scroll[q] = 0;

        int track_top = r.top + Ui_S(QUAD_HEAD_H) + Ui_S(2);
        int track_bot = r.bottom - Ui_S(8);
        int th = track_bot - track_top;
        if (th > 0) {
            RECT tk = { r.right - Ui_S(6), track_top, r.right - Ui_S(3), track_bot };
            Ui_FillRound(hdc, &tk, Ui_S(2), mix_color(bg, fg, t->dark ? 22 : 12));
            int bh = th * vis / n;
            if (bh < Ui_S(16)) bh = Ui_S(16);
            int by = track_top + (th - bh) * app->quad_scroll[q] / (maxs > 0 ? maxs : 1);
            RECT bb = { tk.left, by, tk.right, by + bh };
            Ui_FillRound(hdc, &bb, Ui_S(2), mix_color(bg, fg, 55));
        }
    }
}

void Ui_PaintTodoQuad(AppState* app, HDC hdc) {
    for (int q = 0; q < TODO_QUAD_COUNT; q++)
        draw_quad_card(app, hdc, q);
}

/* ---- 记账页「支出 / 收入」分段（待办页已改用下拉框，这里只剩记账） ---- */

static void draw_summary_card(AppState* app, HDC hdc, const RECT* rc) {
    const Theme* t = Theme_Now();
    Ui_GradientRound(hdc, rc, Ui_S(10), t->sum_a, t->sum_b, 0);
    if (t->dark) Ui_StrokeRound(hdc, rc, Ui_S(10), t->border, 1.0f);

    char month[16];
    double expense = 0, income = 0;
    int n = 0;
    acct_month_cur(app, month, sizeof(month));
    acct_month_sum(app, month, &expense, &income, &n);

    int pad = Ui_S(18);
    RECT lr = { rc->left + pad, rc->top + Ui_S(10), rc->right - pad, rc->top + Ui_S(28) };

    /* 标题跟着筛选月份走：本月才写「本月支出」，切到别的月就写年月 */
    {
        char now_m[16];
        char today[DATE_STR_LEN];
        get_today(today, sizeof(today));
        get_month_from_date(today, now_m, sizeof(now_m));
        WCHAR title[64];
        if (strcmp(month, now_m) == 0) {
            wcscpy(title, L"本月支出");
        } else {
            int y = 0, m = 0;
            if (sscanf(month, "%d-%d", &y, &m) == 2)
                _snwprintf(title, 64, L"%d 年 %d 月支出", y, m);
            else
                wcscpy(title, L"本月支出");
            title[63] = 0;
        }
        Ui_Text(hdc, title, &lr, t->sum_sub, Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    WCHAR amt[64];
    fmt_money(amt, 64, expense);
    RECT vr = { rc->left + pad, rc->top + Ui_S(26), rc->right - pad, rc->top + Ui_S(68) };
    Ui_Text(hdc, amt, &vr, t->sum_fg, Ui_Font(UI_FONT_BIG),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    /* 右上角月份胶囊：点了弹月份选择器 */
    {
        RECT mr = Ui_SumMonthRect(app->hAcctPanel);

        COLORREF chip;
        if (t->dark) chip = app->hover_sum_mon ? RGB(0x3A, 0x3E, 0x46) : RGB(0x2A, 0x2D, 0x33);
        else         chip = app->hover_sum_mon ? RGB(0x2A, 0x86, 0xC8) : RGB(0x1F, 0x79, 0xBC);
        Ui_FillRound(hdc, &mr, Ui_S(6), chip);

        WCHAR mt[32];
        int y = 0, m = 0;
        if (sscanf(month, "%d-%d", &y, &m) == 2) _snwprintf(mt, 32, L"%d-%02d", y, m);
        else wcscpy(mt, L"----");
        mt[31] = 0;

        RECT tr = mr;
        tr.left += Ui_S(10);
        tr.right -= Ui_S(20);
        Ui_Text(hdc, mt, &tr, t->sum_sub, Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* 小三角 */
        int cx = mr.right - Ui_S(12);
        int cy = (mr.top + mr.bottom) / 2;
        int q = Ui_S(3);
        POINT p[3] = { { cx - q, cy - q / 2 }, { cx + q, cy - q / 2 }, { cx, cy + q } };
        HBRUSH br = CreateSolidBrush(t->sum_sub);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        HPEN pen = CreatePen(PS_SOLID, 1, t->sum_sub);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        Polygon(hdc, p, 3);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(br);
        DeleteObject(pen);
    }

    /* 副行：收入、结余、笔数。结余为负时 fmt_money 会自己带负号 */
    WCHAR sub[192], winc[64], wbal[64];
    fmt_money(winc, 64, income);
    fmt_money(wbal, 64, income - expense);
    _snwprintf(sub, 192, L"收入 %ls · 结余 %ls · 共 %d 笔", winc, wbal, n);
    sub[191] = 0;
    RECT sr = { rc->left + pad, rc->top + Ui_S(68), rc->right - pad, rc->top + Ui_S(86) };
    Ui_Text(hdc, sub, &sr, t->sum_sub, Ui_Font(UI_FONT_SMALL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}


void Ui_PaintPanel(AppState* app, HWND panel, HDC hdc) {
    RECT rc;
    GetClientRect(panel, &rc);
    const Theme* t = Theme_Now();
    Ui_FillSolid(hdc, &rc, t->bg);

    int PAD = Ui_S(UI_PAD);
    int is_acct = (panel == app->hAcctPanel);
    int cardtop = is_acct ? Ui_AcctCardTop() : Ui_CardTop();

    if (is_acct) {
        RECT sum = { PAD, Ui_CardTop(), rc.right - PAD, Ui_SumBottom() };
        draw_summary_card(app, hdc, &sum);
    }

    RECT card = { PAD, cardtop, rc.right - PAD, Ui_CardBottom(cardtop) };
    Ui_Card(hdc, &card, Ui_S(10));

    /* 补窗口左右外框（面板子窗口覆盖了主窗口的这部分） */
    {
        RECT e = { 0, 0, 1, rc.bottom };
        Ui_FillSolid(hdc, &e, t->border);
        e.left = rc.right - 1; e.right = rc.right;
        Ui_FillSolid(hdc, &e, t->border);
    }

    int listw = rc.right - PAD * 2;
    int hdr_y = Ui_HeaderY(Ui_CardBottom(cardtop));
    int fitw = list_fit_width(listw);

    if (panel == app->hTodoPanel) {
        draw_slot_label(hdc, app->hTodoTitle, L"标题");
        draw_slot_label(hdc, app->hTodoContent, L"内容");
        draw_slot_only(hdc, app->hTodoSearch);
        if (app->hTodoPrioCombo) draw_field_label(hdc, app->hTodoPrioCombo, L"优先级");
        Ui_PaintTodoQuad(app, hdc);
    } else if (panel == app->hAcctPanel) {
        draw_slot_label(hdc, app->hAcctAmount, L"金额");
        draw_slot_label(hdc, app->hAcctNote, L"备注");
        draw_slot_only(hdc, app->hAcctSearch);

        /* 分类是自绘下拉框：槽（背景+描边+箭头）由控件自己画，这里只补左边的标签 */
        draw_field_label(hdc, app->hAcctCatCombo, L"分类");
        draw_date_field(hdc, app->hAcctDate);
        draw_acct_type_seg(app, hdc);

        int cols[4];
        calc_acct_cols(fitw, cols);
        static const WCHAR* names[4] = { L"日期", L"分类", L"金额", L"备注" };
        static const int ra[4] = { 0, 0, 1, 0 };
        draw_header_row(hdc, PAD, hdr_y, fitw, cols, names, ra);
    }
}

/* ---------------- 倒计时条绘制 ---------------- */

void Ui_RefreshTimerBar(AppState* app) {
    if (app->hTimerBar) {
        InvalidateRect(app->hTimerBar, NULL, FALSE);
    }
}


void Ui_PaintTimerBar(AppState* app, HDC hdc) {
    RECT rc;
    GetClientRect(app->hTimerBar, &rc);
    const Theme* t = Theme_Now();

    Ui_FillSolid(hdc, &rc, t->bg);
    Ui_Card(hdc, &rc, Ui_S(10));

    RECT lr = { Ui_S(14), 0, Ui_S(58), rc.bottom };
    Ui_Text(hdc, L"倒计时", &lr, t->text_sub, Ui_Font(UI_FONT_SMALL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    draw_slot_tight(hdc, app->hTimerHour);
    draw_slot_tight(hdc, app->hTimerMin);
    draw_slot_tight(hdc, app->hTimerSec);

    {
        static const WCHAR* units[3] = { L"时", L"分", L"秒" };
        HWND edits[3] = { app->hTimerHour, app->hTimerMin, app->hTimerSec };
        for (int i = 0; i < 3; i++) {
            RECT s;
            edit_slot_tight(edits[i], &s);
            if (s.right <= s.left) continue;
            RECT ur = { s.right + Ui_S(6), 0, s.right + Ui_S(22), rc.bottom };
            Ui_Text(hdc, units[i], &ur, t->text_sub, Ui_Font(UI_FONT_SMALL),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    {
        WCHAR buf[16];
        int hh = app->timer_seconds / 3600;
        int mm = (app->timer_seconds % 3600) / 60;
        int ss = app->timer_seconds % 60;
        swprintf(buf, 16, L"%02d:%02d:%02d", hh, mm, ss);
        RECT nr = { Ui_S(418), 0, rc.right - Ui_S(16), rc.bottom };
        Ui_Text(hdc, buf, &nr, t->accent, Ui_Font(UI_FONT_MONO),
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ---------------- 命中测试 ---------------- */

static int pt_in(const RECT* r, POINT pt) {
    return pt.x >= r->left && pt.x < r->right && pt.y >= r->top && pt.y < r->bottom;
}

int Ui_HitTopbar(AppState* app, POINT pt) {
    if (pt.y < 0 || pt.y >= app->rt_close.bottom) return HIT_NONE;
    if (pt_in(&app->rt_close, pt)) return HIT_CLOSE;
    if (pt_in(&app->rt_max, pt))   return HIT_MAX;
    if (pt_in(&app->rt_min, pt))   return HIT_MIN;
    if (pt_in(&app->rt_file, pt))  return HIT_FILE;
    /* 天气文字只有 24 高，命中区放到整条标题栏，双击更好落 */
    if (app->rt_weather.right > app->rt_weather.left) {
        RECT w = app->rt_weather;
        w.top = 0;
        w.bottom = app->rt_close.bottom;
        if (pt_in(&w, pt)) return HIT_WEATHER;
    }
    if (pt_in(&app->rt_theme, pt)) return HIT_THEME;
    if (pt_in(&app->rt_tab0, pt))  return HIT_TAB0;
    if (pt_in(&app->rt_tab1, pt))  return HIT_TAB1;
    return HIT_NONE;
}

/* ---------------- 文件菜单 ---------------- */

typedef struct {
    int         id;
    const char* label;   /* UTF-8；NULL 表示分隔线 */
} MenuItemDef;

/*
 * 【扩展位】后续新增功能在 kFileMenu 里加一行即可，
 * ID 从 3003 起，并在 app.c 的 WM_COMMAND 里加对应分支。
 * label 为 NULL 表示分隔线。
 */
static const MenuItemDef kFileMenu[] = {
    { ID_MENU_BACKUP, "备份数据…" },
    { ID_MENU_IMPORT, "导入数据…" },
    { ID_MENU_EXPORT_CSV, "导出记账 CSV…" },
    { 0,              NULL },
    { ID_MENU_BUDGET, "预算设置…" },
    { ID_MENU_TRASH,  "最近删除…" },
    { 0,              NULL },
    { ID_TRAY_EXIT,   "退出" },
};

#define MENU_ITEM_MAX 24
static WCHAR g_menu_text[MENU_ITEM_MAX][128];

void Ui_ShowFileMenu(AppState* app, HWND hwnd) {
    int n = (int)(sizeof(kFileMenu) / sizeof(kFileMenu[0]));
    if (n > MENU_ITEM_MAX) n = MENU_ITEM_MAX;

    UiMenuItem items[MENU_ITEM_MAX];
    memset(items, 0, sizeof(items));          /* dot = 0：文件菜单不画色点 */
    for (int i = 0; i < n; i++) {
        if (!kFileMenu[i].label) {
            items[i].id = 0;
            items[i].label = NULL;
        } else {
            utf8_to_wchar(kFileMenu[i].label, g_menu_text[i], 128);
            items[i].id = kFileMenu[i].id;
            items[i].label = g_menu_text[i];
        }
    }

    int cmd = Ui_PopMenuAt(hwnd, &app->rt_file, items, n);
    if (cmd) SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), 0);
}

void Ui_MeasureMenuItem(MEASUREITEMSTRUCT* mis) {
    if (!mis || mis->CtlType != ODT_MENU) return;
    const WCHAR* s = (const WCHAR*)mis->itemData;
    if (!s || !s[0]) {
        mis->itemHeight = Ui_S(9);
        mis->itemWidth  = Ui_S(150);
        return;
    }
    HDC dc = GetDC(NULL);
    int w = Ui_TextWidth(dc, s, Ui_Font(UI_FONT_BODY));
    ReleaseDC(NULL, dc);
    mis->itemHeight = Ui_S(30);
    mis->itemWidth  = w + Ui_S(32);
}

void Ui_DrawMenuItem(LPDRAWITEMSTRUCT dis) {
    if (!dis || dis->CtlType != ODT_MENU) return;
    const Theme* t = Theme_Now();
    RECT rc = dis->rcItem;
    const WCHAR* s = (const WCHAR*)dis->itemData;

    if (!s || !s[0]) {
        Ui_FillSolid(dis->hDC, &rc, t->surface);
        int y = (rc.top + rc.bottom) / 2;
        RECT ln = { rc.left + Ui_S(10), y, rc.right - Ui_S(10), y + 1 };
        Ui_FillSolid(dis->hDC, &ln, t->border);
        return;
    }

    int sel = (dis->itemState & ODS_SELECTED) != 0;
    Ui_FillSolid(dis->hDC, &rc, sel ? t->accent_soft : t->surface);

    RECT tr = rc;
    tr.left += Ui_S(16);
    Ui_Text(dis->hDC, s, &tr, sel ? t->accent_soft_fg : t->text,
            Ui_Font(UI_FONT_BODY),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

/* ---------------- 分类下拉 ---------------- */

/* 精简后的 8 个标准分类。历史上出现过 14 种手打分类（淘宝/彩票/充电/充值/车/
 * 理发/按摩/红包…），口径分散导致统计没法看，这里统一收口。
 * 定义在 account.c，预算对话框等其它地方也用它，保持单一来源。 */
#define CATEGORY_COUNT ACCT_STD_CAT_N

/*
 * 说明：原来这里有一套「只读 Edit + 单击弹自绘菜单」的分类选择器
 * （cat_sub / Ui_CatEditGuard / Ui_PopCategoryMenu / Ui_ShowCategoryMenu）。
 * 弹出层是独立窗口 + 嵌套消息循环，点击穿透时会反复弹，屏幕上会叠出好几层。
 * 现在统一改用 menu.c 里的自绘下拉框 Ui_ComboCreate，这套已删除。
 */

void Ui_ApplyTheme(AppState* app) {
    const Theme* t = Theme_Now();

    if (g_br_input) { DeleteObject(g_br_input); g_br_input = NULL; }
    g_br_input = CreateSolidBrush(t->input_bg);

    if (app->hAcctList) {
        ListView_SetBkColor(app->hAcctList, t->list_bg);
        ListView_SetTextBkColor(app->hAcctList, t->list_bg);
        ListView_SetTextColor(app->hAcctList, t->text);
        InvalidateRect(app->hAcctList, NULL, TRUE);
    }

    /* 标题栏已整条自绘，不再需要 DWM 深色属性 */
    HWND parent = GetParent(app->hTodoPanel);
    if (parent) InvalidateRect(parent, NULL, TRUE);
    if (app->hTodoPanel) InvalidateRect(app->hTodoPanel, NULL, TRUE);
    if (app->hAcctPanel) InvalidateRect(app->hAcctPanel, NULL, TRUE);
    if (app->hTimerBar)  InvalidateRect(app->hTimerBar, NULL, TRUE);

    RedrawWindow(parent, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

HBRUSH Ui_InputBrush(void) {
    if (!g_br_input) g_br_input = CreateSolidBrush(Theme_Now()->input_bg);
    return g_br_input;
}

/* ---------------- 按钮自绘 ---------------- */

int Ui_ButtonKind(int id) {
    switch (id) {
    case ID_TODO_ADD:
    case ID_ACCT_ADD:
    case ID_TMR_START:
        return UI_BTN_PRIMARY;
    default:
        return UI_BTN_SECONDARY;
    }
}

void Ui_DrawOwnerdrawButton(LPDRAWITEMSTRUCT dis) {
    if (!dis || dis->CtlType != ODT_BUTTON) return;
    const Theme* t = Theme_Now();
    RECT rc = dis->rcItem;

    /* 分类字段右下角的下拉箭头：只画填充和箭头，外框由父面板统一绘制 */
    if ((int)dis->CtlID == ID_ACCT_CAT_DROP) {
        int st = UI_ST_NORMAL;
        if (dis->itemState & ODS_SELECTED) st = UI_ST_PRESSED;
        else if (GetPropW(dis->hwndItem, L"hovered")) st = UI_ST_HOVER;

        Ui_FillSolid(dis->hDC, &rc, t->input_bg);
        COLORREF fg = (st == UI_ST_NORMAL) ? t->text_dim : t->accent_soft_fg;

        HPEN pen = CreatePen(PS_SOLID, 1, fg);
        HBRUSH br = CreateSolidBrush(fg);
        HPEN op = (HPEN)SelectObject(dis->hDC, pen);
        HBRUSH ob = (HBRUSH)SelectObject(dis->hDC, br);

        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2 - (st == UI_ST_PRESSED ? -1 : 0);
        int s = Ui_S(3);
        POINT tri[3] = { { cx - s, cy - s / 2 }, { cx + s, cy - s / 2 }, { cx, cy + s } };
        Polygon(dis->hDC, tri, 3);

        SelectObject(dis->hDC, op);
        SelectObject(dis->hDC, ob);
        DeleteObject(pen);
        DeleteObject(br);
        return;
    }

    /* 待办优先级的四个单选按钮：选中时铺语义色浅底 + 语义色文字。
       "prio" 属性 = 象限号 + 1（1 重要且紧急 / 2 重要不紧急 / 3 紧急不重要 / 4 不重要不紧急，
       0 表示不是优先级按钮）。 */
    {
        int pv = (int)(INT_PTR)GetPropW(dis->hwndItem, L"prio");
        if (pv > 0 && GetPropW(dis->hwndItem, L"primary")) {
            COLORREF pc = Ui_QuadColor(pv - 1);
            RECT b = rc;
            b.right  -= 1;
            b.bottom -= 1;
            Ui_FillRound(dis->hDC, &b, Ui_S(7),
                         mix_color(t->input_bg, pc, t->dark ? 34 : 16));
            Ui_StrokeRound(dis->hDC, &b, Ui_S(7),
                           mix_color(t->input_bg, pc, t->dark ? 62 : 45), 1.0f);
            WCHAR text[128];
            GetWindowTextW(dis->hwndItem, text, 128);
            Ui_Text(dis->hDC, text, &b, pc, Ui_Font(UI_FONT_SMALL),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }
    }

    Ui_FillSolid(dis->hDC, &rc, t->surface);

    int state = UI_ST_NORMAL;
    if (dis->itemState & ODS_DISABLED) state = UI_ST_DISABLED;
    else if (dis->itemState & ODS_SELECTED) state = UI_ST_PRESSED;
    else if (GetPropW(dis->hwndItem, L"hovered")) state = UI_ST_HOVER;

    WCHAR text[128];
    GetWindowTextW(dis->hwndItem, text, 128);
    Ui_DrawButton(dis->hDC, &rc, text, Ui_ButtonKind((int)dis->CtlID), state);
}

/* ---------------- ListView 自绘 ---------------- */

LRESULT Ui_ListCustomDraw(LPNMLVCUSTOMDRAW cd) {
    if (!cd) return CDRF_DODEFAULT;
    const Theme* t = Theme_Now();
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: {
        HBRUSH br = CreateSolidBrush(t->list_bg);
        FillRect(cd->nmcd.hdc, &cd->nmcd.rc, br);
        DeleteObject(br);
        return CDRF_NOTIFYITEMDRAW;
    }
    case CDDS_ITEMPREPAINT: {
        int idx = (int)cd->nmcd.dwItemSpec;
        LRESULT st = SendMessageW(cd->nmcd.hdr.hwndFrom, LVM_GETITEMSTATE,
                                  (WPARAM)idx, LVIS_SELECTED);
        int sel = ((int)st & LVIS_SELECTED) != 0;
        COLORREF bg = sel ? t->selection
                          : ((idx & 1) ? t->surface_alt : t->list_bg);
        /* 关键：清掉选中标记，否则返回后系统仍会用默认高亮蓝覆盖我们刷的底色；
           选中态的高亮由我们自己 FillRect 画 */
        if (sel) cd->nmcd.uItemState &= ~CDIS_SELECTED;
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(cd->nmcd.hdc, &cd->nmcd.rc, br);
        DeleteObject(br);
        cd->clrTextBk = bg;
        /* 待办列表：整行文字按优先级上色（红=紧急 / 橙=重要 / 默认=不重要） */
        int prio_col = (int)(INT_PTR)GetPropW(cd->nmcd.hdr.hwndFrom, L"prioCol") - 1;
        int prio_row = (int)(((DWORD)cd->nmcd.lItemlParam >> 25) & 3);
        /* 总计行（色号哨兵 0xF）：整行含金额、占比统一用主题强调色 + 半粗，
           和上面的分类明细行区分开 */
        int ci_row = (int)(((DWORD)cd->nmcd.lItemlParam >> 25) & 0xF);
        int inc_row = (int)(((DWORD)cd->nmcd.lItemlParam >> 29) & 1);
        int over_row = (int)(((DWORD)cd->nmcd.lItemlParam >> 30) & 1);
        if (prio_col >= 0) {
            if (sel)                                  cd->clrText = t->accent_soft_fg;
            else if (prio_row == TODO_PRIO_URGENT)    cd->clrText = t->danger;
            else if (prio_row == TODO_PRIO_IMPORTANT) cd->clrText = t->warning;
            else                                      cd->clrText = t->text;
        } else if (ci_row == 0xF) {
            cd->clrText = t->accent;
            SelectObject(cd->nmcd.hdc, Ui_Font(UI_FONT_LABEL));
        } else if (over_row) {
            /* 超预算的分类整行标红 */
            cd->clrText = t->danger;
        } else if (inc_row) {
            /* 收入行：金额用成功绿，跟支出的正文色一眼分得开 */
            cd->clrText = t->success;
        } else {
            cd->clrText = sel ? t->accent_soft_fg : t->text;
        }
        return CDRF_NEWFONT | CDRF_NOTIFYSUBITEMDRAW;
    }
    case (CDDS_ITEMPREPAINT | CDDS_SUBITEM): {
        /*
         * 分类列画成小圆角色块（mockup 里的 chip）；
         * 备注列自己画，好给紧挨着的金额列让出一点间距（列表列没有内边距可设）。
         */
        HWND lv = cd->nmcd.hdr.hwndFrom;
        int chipcol  = (int)(INT_PTR)GetPropW(lv, L"chipCol") - 1;
        int notecol  = (int)(INT_PTR)GetPropW(lv, L"noteCol") - 1;
        int checkcol = (int)(INT_PTR)GetPropW(lv, L"checkCol") - 1;
        int sub = (int)cd->iSubItem;
        if (sub != chipcol && sub != notecol && sub != checkcol) return CDRF_DODEFAULT;

        int idx = (int)cd->nmcd.dwItemSpec;

        /* 勾选列：空心圆 / 绿色对勾，直接画，不用字符 */
        if (sub == checkcol) {
            RECT sr;
            if (sub == 0) {
                /* LVM_GETSUBITEMRECT 对第 0 列无效，只能用 LVM_GETITEMRECT 拿整行再裁 */
                sr.left = LVIR_BOUNDS;
                if (!SendMessageW(lv, LVM_GETITEMRECT, (WPARAM)idx, (LPARAM)&sr))
                    return CDRF_DODEFAULT;
                int cw = (int)SendMessageW(lv, LVM_GETCOLUMNWIDTH, 0, 0);
                if (cw > 0) sr.right = sr.left + cw;
            } else {
                memset(&sr, 0, sizeof(sr));
                sr.top  = (LONG)sub;
                sr.left = LVIR_BOUNDS;
                SendMessageW(lv, LVM_GETSUBITEMRECT, (WPARAM)idx, (LPARAM)&sr);
            }
            if (sr.right <= sr.left) return CDRF_DODEFAULT;

            int done = (int)(((DWORD)cd->nmcd.lItemlParam >> 24) & 1);

            /* 优先级色条：贴行左边缘，紧急红 / 重要橙 / 不重要不画 */
            if ((int)(INT_PTR)GetPropW(lv, L"prioCol") - 1 >= 0) {
                int prio = (int)(((DWORD)cd->nmcd.lItemlParam >> 25) & 3);
                if (prio > 0) {
                    RECT bar = { sr.left + Ui_S(2), sr.top + Ui_S(5),
                                 sr.left + Ui_S(5), sr.bottom - Ui_S(5) };
                    if (bar.right > bar.left && bar.bottom > bar.top) {
                        COLORREF pc = (prio == TODO_PRIO_URGENT) ? t->danger : t->warning;
                        Ui_FillRound(cd->nmcd.hdc, &bar, Ui_S(2), pc);
                    }
                }
            }

            Ui_DrawCheckMark(cd->nmcd.hdc, (sr.left + sr.right) / 2,
                             (sr.top + sr.bottom) / 2, Ui_S(15),
                             done, t->success, t->text_sub);
            return CDRF_SKIPDEFAULT;
        }

        WCHAR buf[256];
        memset(buf, 0, sizeof(buf));
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.iSubItem = sub;
        it.pszText = buf;
        it.cchTextMax = 256;
        SendMessageW(lv, LVM_GETITEMTEXTW, (WPARAM)idx, (LPARAM)&it);

        RECT sr;
        memset(&sr, 0, sizeof(sr));
        sr.top  = (LONG)sub;         /* LVM_GETSUBITEMRECT 要靠 top 传列号 */
        sr.left = LVIR_BOUNDS;
        SendMessageW(lv, LVM_GETSUBITEMRECT, (WPARAM)idx, (LPARAM)&sr);
        if (sr.right <= sr.left) return CDRF_DODEFAULT;
        if (!buf[0]) return CDRF_DODEFAULT;

        LRESULT stl = SendMessageW(lv, LVM_GETITEMSTATE, (WPARAM)idx, LVIS_SELECTED);
        int sel = ((int)stl & LVIS_SELECTED) != 0;
        int ci_sub = (int)(((DWORD)cd->nmcd.lItemlParam >> 25) & 0xF);
        int is_total = (ci_sub == 0xF);
        int over_sub = (int)(((DWORD)cd->nmcd.lItemlParam >> 30) & 1);
        int inc_sub  = (int)(((DWORD)cd->nmcd.lItemlParam >> 29) & 1);
        COLORREF fg;
        HFONT hfn;
        if (is_total) {
            fg = t->accent;
            hfn = Ui_Font(UI_FONT_LABEL);
        } else {
            fg = over_sub ? t->danger : (inc_sub ? t->success
                                                : (sel ? t->accent_soft_fg : t->text));
            hfn = Ui_Font(UI_FONT_SMALL);
        }

        if (sub == notecol) {
            RECT tr = sr;
            tr.left += Ui_S(12);
            tr.right -= Ui_S(4);
            Ui_Text(cd->nmcd.hdc, buf, &tr, fg, hfn,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            return CDRF_SKIPDEFAULT;
        }

        int tw = Ui_TextWidth(cd->nmcd.hdc, buf, Ui_Font(UI_FONT_SMALL));
        int chh = sr.bottom - sr.top;
        int hh  = chh - Ui_S(10);
        if (hh < Ui_S(16)) hh = Ui_S(16);
        int top = sr.top + (chh - hh) / 2;
        RECT cr = { sr.left + Ui_S(6), top,
                    sr.left + Ui_S(6) + tw + Ui_S(18), top + hh };

        /* 颜色按分类取（高 4 位），同一分类在浅/深主题下各有一套配色；0xF = 不画色块（总计行） */
        int ci = ci_sub;
        if (is_total) {
            RECT tr = sr;
            tr.left += Ui_S(6);
            Ui_Text(cd->nmcd.hdc, buf, &tr, fg, hfn,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            return CDRF_SKIPDEFAULT;
        }
        COLORREF cbg, cfg;
        Ui_CatChipColors(ci, &cbg, &cfg);
        Ui_FillRound(cd->nmcd.hdc, &cr, hh / 3, cbg);

        Ui_Text(cd->nmcd.hdc, buf, &cr, cfg, Ui_Font(UI_FONT_SMALL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return CDRF_SKIPDEFAULT;
    }
    default:
        break;
    }
    return CDRF_DODEFAULT;
}

/* ---------------- 自绘滚动条 ---------------- */
/*
 * 原生 ListView 滚动条在深色主题下是一块浅色条（Win10 1809 无法着色），
 * 这里把它隐藏，叠一个 10px 宽的细滚动条，颜色跟随主题。
 */

#define SCROLL_CLASS L"TodoScroll"

/* 从列表实际几何算可见行数：LVM_GETCOUNTPERPAGE 在自绘行高下并不可靠，
 * 会把「刚好放得下」的情况判成还能滚动，画出一条满高的滑杆。 */
static void sb_metrics(HWND list, int* total, int* per, int* top) {
    *total = (int)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0);
    *per = 1;
    *top = 0;
    if (*total <= 0) return;

    *top = (int)SendMessageW(list, LVM_GETTOPINDEX, 0, 0);

    RECT rc, fr;
    GetClientRect(list, &rc);
    memset(&fr, 0, sizeof(fr));
    fr.left = LVIR_BOUNDS;
    if (!SendMessageW(list, LVM_GETITEMRECT, *top, (LPARAM)&fr)) return;

    int row_h = fr.bottom - fr.top;
    /* 行距用相邻两行的位置差来量，比单项高度可靠（单项 rect 可能只覆盖文字） */
    if (*top + 1 < *total) {
        RECT fr2;
        memset(&fr2, 0, sizeof(fr2));
        fr2.left = LVIR_BOUNDS;
        if (SendMessageW(list, LVM_GETITEMRECT, *top + 1, (LPARAM)&fr2)) {
            int pitch = fr2.top - fr.top;
            if (pitch > 0) row_h = pitch;
        }
    }
    if (row_h < 1) row_h = 1;

    int avail = rc.bottom - fr.top;
    int vis = avail / row_h;

    /* 全部放得下就不画滑杆 */
    if (vis >= *total && *top == 0) *per = *total;
    else *per = (vis < 1) ? 1 : vis;
}

static void sb_thumb(HWND sb, HWND list, RECT* out, int* max_top) {
    RECT rc;
    GetClientRect(sb, &rc);
    int total, per, top;
    sb_metrics(list, &total, &per, &top);

    *max_top = total - per;
    if (*max_top < 0) *max_top = 0;

    int track_h = rc.bottom - rc.top;
    int min_h = Ui_S(26);
    int h = track_h;
    if (total > per && total > 0) h = track_h * per / total;
    if (h < min_h && total > per) h = min_h;
    if (h > track_h) h = track_h;

    int y = 0;
    if (*max_top > 0) {
        int span = track_h - h;
        y = span * top / *max_top;
    }
    /*
     * 覆盖层是整条原生槽位宽（21px 左右），底板同列表色把系统白槽盖掉，
     * 里面只画一条 10px 的细滑杆，靠右留 4px。
     */
    out->right = rc.right - Ui_S(5);
    out->left = out->right - Ui_S(10);
    out->top = rc.top + y;
    out->bottom = out->top + h;
}

static void sb_paint(HWND sb, HWND list, HDC hdc0) {
    const Theme* t = Theme_Now();
    RECT rc;
    GetClientRect(sb, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    /* 底板刷成和列表一致 */
    Ui_FillSolid(hdc0, &rc, t->list_bg);

    int total, per, top;
    sb_metrics(list, &total, &per, &top);
    if (total <= per) return;   /* 内容不足一屏 */

    RECT th;
    int max_top = 0;
    sb_thumb(sb, list, &th, &max_top);

    COLORREF c = GetPropW(sb, L"hover") ? t->text_dim : t->border;
    int r = (th.right - th.left) / 2;
    Ui_FillRound(hdc0, &th, r, c);
}

static LRESULT CALLBACK sb_proc(HWND sb, UINT msg, WPARAM wp, LPARAM lp) {
    HWND list = (HWND)GetWindowLongPtrW(sb, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(sb, &ps);
        if (list) sb_paint(sb, list, hdc);
        EndPaint(sb, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (list) {
            RECT th;
            int max_top;
            sb_thumb(sb, list, &th, &max_top);
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

            if (GetCapture() == sb) {
                int track = 0;
                RECT rc;
                GetClientRect(sb, &rc);
                track = (rc.bottom - rc.top) - (th.bottom - th.top);
                if (track > 0 && max_top > 0) {
                    int off = (int)(INT_PTR)GetPropW(sb, L"drag_off");
                    int y = p.y - off;
                    if (y < 0) y = 0;
                    if (y > track) y = track;
                    int new_top = MulDiv(y, max_top, track);
                    SendMessageW(list, LVM_ENSUREVISIBLE, (WPARAM)new_top, FALSE);
                    InvalidateRect(sb, NULL, FALSE);
                }
                return 0;
            }

            int hv = PtInRect(&th, p) ? 1 : 0;
            if (hv != (int)(INT_PTR)GetPropW(sb, L"hover")) {
                if (hv) SetPropW(sb, L"hover", (HANDLE)1);
                else RemovePropW(sb, L"hover");
                InvalidateRect(sb, NULL, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, sb, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (GetPropW(sb, L"hover")) {
            RemovePropW(sb, L"hover");
            InvalidateRect(sb, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (list) {
            RECT th;
            int max_top;
            sb_thumb(sb, list, &th, &max_top);
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&th, p)) {
                SetPropW(sb, L"drag_off", (HANDLE)(INT_PTR)(p.y - th.top));
                SetCapture(sb);
            } else if (max_top > 0) {
                /* 点轨道：上下翻一屏 */
                int total, per, top;
                sb_metrics(list, &total, &per, &top);
                int delta = (p.y < th.top) ? -per : per;
                int nt = top + delta;
                if (nt < 0) nt = 0;
                if (nt > max_top) nt = max_top;
                SendMessageW(list, LVM_ENSUREVISIBLE, (WPARAM)nt, FALSE);
                InvalidateRect(sb, NULL, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (GetCapture() == sb) ReleaseCapture();
        return 0;

    case WM_MOUSEWHEEL:
        if (list) {
            SendMessageW(list, WM_MOUSEWHEEL, wp, lp);
            InvalidateRect(sb, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(sb, msg, wp, lp);
}

static LRESULT CALLBACK list_sub(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    LRESULT r = DefSubclassProc(h, msg, wp, lp);
    HWND sb = (HWND)GetPropW(h, L"sb");
    if (sb) {
        switch (msg) {
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_SIZE:
        case LVM_INSERTITEMW:
        case LVM_DELETEALLITEMS:
        case LVM_SETITEMCOUNT:
            InvalidateRect(sb, NULL, FALSE);
            break;
        }
    }
    return r;
}

static int sb_class_ready = 0;

/* 在列表右边缘叠一条自绘滚动条；每次布局后调用即可跟随移动 */
void Ui_ScrollAttach(HWND hList) {
    if (!hList) return;
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hList, GWLP_HINSTANCE);

    if (!sb_class_ready) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = sb_proc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = SCROLL_CLASS;
        RegisterClassW(&wc);
        sb_class_ready = 1;
    }

    HWND sb = (HWND)GetPropW(hList, L"sb");
    if (!sb) {
        HWND parent = GetParent(hList);
        sb = CreateWindowExW(0, SCROLL_CLASS, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             0, 0, 10, 10, parent, NULL, hInst, NULL);
        if (!sb) return;
        SetWindowLongPtrW(sb, GWLP_USERDATA, (LONG_PTR)hList);
        SetPropW(hList, L"sb", (HANDLE)sb);
        SetWindowSubclass(hList, list_sub, 7, 0);
    }

    /*
     * 原生纵向滚动条只 Hide 掉，不摘样式：摘掉后 ListView 仍会在右侧留一条
     * 系统白的空槽（深色主题下极刺眼）。改成让自绘滚动条整条盖住那条槽位。
     */
    ShowScrollBar(hList, SB_VERT, FALSE);

    RECT lr;
    GetWindowRect(hList, &lr);
    POINT p = { lr.left, lr.top };
    ScreenToClient(GetParent(hList), &p);

    int w = GetSystemMetrics(SM_CXVSCROLL);
    if (w < Ui_S(12)) w = Ui_S(12);
    SetWindowPos(sb, HWND_TOP, p.x + (lr.right - lr.left) - w, p.y,
                 w, lr.bottom - lr.top, SWP_NOACTIVATE);
    InvalidateRect(sb, NULL, FALSE);
}

void Ui_ScrollDetach(HWND hList) {
    if (!hList) return;
    HWND sb = (HWND)GetPropW(hList, L"sb");
    if (sb) {
        RemovePropW(hList, L"sb");
        DestroyWindow(sb);
    }
    ShowScrollBar(hList, SB_VERT, TRUE);
}
