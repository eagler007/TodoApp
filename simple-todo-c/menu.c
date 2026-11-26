#include "menu.h"
#include "ui.h"
#include "theme.h"
#include "util.h"
#include <windowsx.h>
#include <stdlib.h>
#include <string.h>

#ifndef CBN_SELCHANGE
#define CBN_SELCHANGE 1
#endif

#ifndef CS_DROPSHADOW
#define CS_DROPSHADOW 0x00020000
#endif

#define MENU_CLASS L"TodoMenu"

#define ITEM_H   30
#define SEP_H    9
#define PAD_X    16
#define MIN_W    120

typedef struct {
    const UiMenuItem* items;
    int n;
    int hover;
    int cur;         /* 当前值对应的 item id（0 = 无），绘制时打标记 */
    int done;
    int result;
    int cols;        /* 列数：分类 8 项用 1 列，每月 1-31 号用 3 列免得比屏幕还高 */
    int col_w;
    int* row_h;      /* 每项高度（分隔线更矮） */
    RECT* row_rc;    /* 每项客户区矩形 */
} MenuState;

static ATOM g_menu_atom = 0;

/* 同一时刻只允许一个弹出层。点分类框时如果上一层的消息循环还没退出，
 * 再进来一次就会叠出第二个窗口（老板截图里叠了三层就是这个原因）。 */
static int g_menu_busy = 0;

static int item_height(const UiMenuItem* it) {
    return (it->label == NULL) ? Ui_S(SEP_H) : Ui_S(ITEM_H);
}

/* 多列布局：按行优先（0,1,2 在同一行）。cols = 1 就是普通竖排菜单。 */
static int menu_rows(const MenuState* s) {
    int c = (s->cols > 0) ? s->cols : 1;
    return (s->n + c - 1) / c;
}

static void layout_rows(MenuState* s, HWND h) {
    int cols = (s->cols > 0) ? s->cols : 1;
    int rows = menu_rows(s);
    int row_y[64];
    int y = Ui_S(6);
    for (int r = 0; r < rows; r++) {
        row_y[r] = y;
        int hh = Ui_S(ITEM_H);
        /* 单列时保留分隔线更矮的处理 */
        if (cols == 1) hh = item_height(&s->items[r * cols]);
        y += hh;
    }

    RECT rc;
    GetClientRect(h, &rc);
    for (int i = 0; i < s->n; i++) {
        int r = i / cols, c = i % cols;
        int hh = (cols == 1) ? item_height(&s->items[i]) : Ui_S(ITEM_H);
        s->row_rc[i].left   = (cols == 1) ? 0 : s->col_w * c;
        s->row_rc[i].right  = (cols == 1) ? rc.right : s->col_w * (c + 1);
        s->row_rc[i].top    = row_y[r];
        s->row_rc[i].bottom = row_y[r] + hh;
    }
}

static void menu_paint(HWND h, MenuState* s, HDC hdc0) {
    const Theme* t = Theme_Now();
    RECT rc;
    GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;

    HDC hdc = CreateCompatibleDC(hdc0);
    HBITMAP bmp = CreateCompatibleBitmap(hdc0, rc.right, rc.bottom);
    HBITMAP ob = (HBITMAP)SelectObject(hdc, bmp);

    Ui_FillSolid(hdc, &rc, t->surface);
    layout_rows(s, h);

    for (int i = 0; i < s->n; i++) {
        const UiMenuItem* it = &s->items[i];
        RECT r = s->row_rc[i];
        if (!it->label) {
            int y = (r.top + r.bottom) / 2;
            RECT ln = { r.left + Ui_S(12), y, r.right - Ui_S(12), y + 1 };
            Ui_FillSolid(hdc, &ln, t->border);
            continue;
        }
        int sel = (s->hover == i);
        if (sel) {
            RECT hr = r;
            InflateRect(&hr, -Ui_S(4), 0);
            Ui_FillRound(hdc, &hr, Ui_S(5), t->accent_soft);
        }
        int is_cur = (s->cur != 0 && it->id == s->cur);

        /* 分类色点只在「当前选中」这一项前面画 —— 一列全是圆点太花，
         * 圆点就是选中标记。其余项保持正文色，靠 hover 高亮指示位置。 */
        COLORREF dot_fg = 0;
        int has_dot = (it->dot > 0 && is_cur);
        if (has_dot) {
            COLORREF dot_bg;
            Ui_CatChipColors(it->dot - 1, &dot_bg, &dot_fg);
            int cx = r.left + Ui_S(12);
            int cy = (r.top + r.bottom) / 2;
            int rad = Ui_S(4);
            RECT dc = { cx - rad, cy - rad, cx + rad + 1, cy + rad + 1 };
            Ui_FillRound(hdc, &dc, rad, dot_fg);
        }

        RECT tr = r;
        tr.left += Ui_S(PAD_X) + (has_dot ? Ui_S(10) : 0);
        COLORREF tc = sel ? t->accent_soft_fg : (has_dot ? dot_fg : t->text);
        Ui_Text(hdc, it->label, &tr, tc,
                Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    {
        RECT b = rc;
        Ui_StrokeRound(hdc, &b, Ui_S(8), t->border, 1.0f);
    }

    BitBlt(hdc0, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, ob);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

static int hit_row(MenuState* s, POINT p) {
    for (int i = 0; i < s->n; i++) {
        if (PtInRect(&s->row_rc[i], p)) return i;
    }
    return -1;
}

static LRESULT CALLBACK MenuProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MenuState* s = (MenuState*)GetWindowLongPtrW(h, GWLP_USERDATA);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        if (s) menu_paint(h, s, hdc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (s) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int i = hit_row(s, p);
            if (i != s->hover) {
                s->hover = i;
                InvalidateRect(h, NULL, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (s && s->hover != -1) {
            s->hover = -1;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (s) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc;
            GetClientRect(h, &rc);
            if (!PtInRect(&rc, p)) {
                s->done = 1;
                s->result = 0;
                return 0;
            }
            int i = hit_row(s, p);
            if (i >= 0 && s->items[i].label) {
                s->result = s->items[i].id;
                s->done = 1;
            }
        }
        return 0;

    case WM_RBUTTONDOWN:
        if (s) { s->done = 1; s->result = 0; }
        return 0;

    case WM_KEYDOWN:
        if (!s) break;
        if (wp == VK_ESCAPE) { s->done = 1; s->result = 0; return 0; }
        if (wp == VK_RETURN) {
            if (s->hover >= 0 && s->items[s->hover].label) {
                s->result = s->items[s->hover].id;
            }
            s->done = 1;
            return 0;
        }
        if (wp == VK_DOWN || wp == VK_UP || wp == VK_LEFT || wp == VK_RIGHT) {
            int step = (s->cols > 0) ? s->cols : 1;
            int dir;
            if (wp == VK_DOWN)       dir = step;
            else if (wp == VK_UP)    dir = -step;
            else if (wp == VK_RIGHT) dir = (step > 1) ? 1 : 1;
            else                     dir = -1;
            int i = s->hover;
            for (int k = 0; k < s->n; k++) {
                i += dir;
                if (i < 0) i = s->n - 1;
                if (i >= s->n) i = 0;
                if (s->items[i].label) break;
            }
            s->hover = i;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && s && !s->done) {
            s->done = 1;
            s->result = 0;
        }
        return 0;

    case WM_CAPTURECHANGED:
        /* 鼠标捕获被别的窗口抢走 —— 菜单再留着就点不掉了，直接收摊 */
        if (s && !s->done) { s->done = 1; s->result = 0; }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int menu_register(HINSTANCE hInst) {
    if (g_menu_atom) return 1;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MenuProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = MENU_CLASS;
    wc.style = CS_DROPSHADOW;
    g_menu_atom = RegisterClassW(&wc);
    return g_menu_atom != 0;
}

int Ui_PopMenuSel(HWND owner, int sx, int sy, int wfix,
                  const UiMenuItem* items, int n, int cur_id) {
    return Ui_PopMenuGrid(owner, sx, sy, wfix, items, n, cur_id, 0);
}

int Ui_PopMenuGrid(HWND owner, int sx, int sy, int wfix,
                   const UiMenuItem* items, int n, int cur_id, int cols_in) {
    if (!owner || !items || n <= 0) return 0;
    if (g_menu_busy) return 0;              /* 已经有弹出层了，别再叠一个 */
    g_menu_busy = 1;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(NULL);
    if (!menu_register(hInst)) { g_menu_busy = 0; return 0; }

    if (n > UI_COMBO_MAX) n = UI_COMBO_MAX;

    /* 项数多就默认排 3 列：31 个数竖着排有 1100+ 像素，比屏幕还高 */
    int cols = (cols_in > 0) ? cols_in : ((n > 12) ? 3 : 1);
    if (cols > n) cols = n;
    if (cols < 1) cols = 1;

    MenuState s;
    memset(&s, 0, sizeof(s));
    s.items = items;
    s.n = n;
    s.hover = -1;
    s.cur = cur_id;
    s.cols = cols;

    int row_h[64];
    RECT row_rc[64];
    s.row_h = row_h;
    s.row_rc = row_rc;

    /* 测宽：单列取最宽项，多列取每列最宽项的平均上限。
     * 圆点只在选中项前面画，宽度由下拉框的固定宽度兜住，这里不用预留。 */
    int colw = Ui_S(MIN_W);
    HDC dc = GetDC(owner);
    for (int i = 0; i < n; i++) {
        if (items[i].label) {
            int tw = Ui_TextWidth(dc, items[i].label, Ui_Font(UI_FONT_SMALL)) + Ui_S(PAD_X) * 2;
            if (tw > colw) colw = tw;
        }
    }
    ReleaseDC(owner, dc);

    int w = colw * cols;
    if (wfix > 0 && wfix > w) {
        w = wfix;
        colw = w / cols;
    }
    s.col_w = colw;

    int rows = (n + cols - 1) / cols;
    int h = Ui_S(12);
    if (cols == 1) {
        for (int i = 0; i < n; i++) h += item_height(&items[i]);
    } else {
        h += rows * Ui_S(ITEM_H);
    }

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (sx + w > wa.right) sx = wa.right - w;
    if (sy + h > wa.bottom) sy -= h;         /* 下面放不下就往上翻 */
    if (sx < wa.left) sx = wa.left;
    if (sy < wa.top) sy = wa.top;

    HWND hw = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MENU_CLASS, L"",
                              WS_POPUP, sx, sy, w, h, owner, NULL, hInst, NULL);
    if (!hw) { g_menu_busy = 0; return 0; }

    SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)&s);
    layout_rows(&s, hw);
    ShowWindow(hw, SW_SHOW);

    /* 抓鼠标：这样点击落在菜单外也能收到 WM_LBUTTONDOWN 从而关闭；
     * 同时避免「第一次点击只用于激活窗口」导致点击穿透到下面的输入框
     * 又弹出一个新菜单。 */
    SetCapture(hw);
    SetFocus(hw);
    SetForegroundWindow(hw);

    MSG msg;
    while (!s.done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (GetCapture() == hw) ReleaseCapture();
    DestroyWindow(hw);
    g_menu_busy = 0;
    return s.result;
}

int Ui_PopMenu(HWND owner, int sx, int sy, const UiMenuItem* items, int n) {
    return Ui_PopMenuSel(owner, sx, sy, 0, items, n, 0);
}

int Ui_PopMenuAt(HWND owner, const RECT* anchor, const UiMenuItem* items, int n) {
    if (!anchor) return 0;
    POINT p = { anchor->left, anchor->bottom + Ui_S(4) };
    ClientToScreen(owner, &p);
    return Ui_PopMenu(owner, p.x, p.y, items, n);
}

/* ================= 自绘下拉框 ================= */
/*
 * 之前分类字段是「只读 Edit + 单击弹菜单」，弹出层是独立窗口 + 嵌套消息循环，
 * 一旦点击穿透就会反复弹（老板看到的三层叠窗）。这里换成真正的下拉框：
 * 外观自绘（跟输入框完全一致），点开复用 Ui_PopMenuSel（有单例守卫）。
 */

#define COMBO_CLASS L"TodoCombo"

typedef struct {
    WCHAR items[UI_COMBO_MAX][32];
    int   n;
    int   sel;      /* -1 = 空 */
    int   hot;
    int   colorize; /* 1 = 按分类名自动取色，左侧画色点 */
    int   dot[UI_COMBO_MAX];   /* -1 = 认不出的分类名（不画点） */
} ComboState;

static ATOM g_combo_atom = 0;

static ComboState* combo_state(HWND h) {
    return (ComboState*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

/* 把某项文本换算成分类色号。认不出来（周几、几号、自定义分类）就 -1。 */
static int combo_dot_of(const WCHAR* w) {
    char u8[64];
    if (!w || !w[0]) return -1;
    wchar_to_utf8(w, u8, 64);
    return Ui_CatPresetIndex(u8);
}

static void combo_paint(HWND h, ComboState* s) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) { EndPaint(h, &ps); return; }

    const Theme* t = Theme_Now();
    int foc = (GetFocus() == h);
    int on  = (s && s->hot) || foc;

    Ui_FillSolid(hdc, &rc, t->input_bg);
    RECT b = rc;
    b.right -= 1;
    b.bottom -= 1;
    Ui_FillRound(hdc, &b, Ui_S(6), t->input_bg);
    Ui_StrokeRound(hdc, &b, Ui_S(6), on ? t->accent : t->input_border, 1.0f);

    /* 文字 */
    RECT tr = rc;
    tr.left  += Ui_S(10);
    tr.right -= Ui_S(26);
    const WCHAR* txt = (s && s->sel >= 0 && s->sel < s->n) ? s->items[s->sel] : L"";

    /* 分类下拉框：选中值左侧画一个分类色点，文字用同色系
     * （和列表里那个彩色分类 chip 是同一套色，一眼能对上） */
    COLORREF tc = t->text;
    if (s && s->colorize && s->sel >= 0 && s->sel < s->n && s->dot[s->sel] >= 0) {
        COLORREF dbg, dfg;
        Ui_CatChipColors(s->dot[s->sel], &dbg, &dfg);
        int cx = rc.left + Ui_S(12);
        int cy = (rc.top + rc.bottom) / 2;
        int rad = Ui_S(4);
        RECT dc = { cx - rad, cy - rad, cx + rad + 1, cy + rad + 1 };
        Ui_FillRound(hdc, &dc, rad, dfg);
        tr.left += Ui_S(12);
        tc = dfg;
    }

    Ui_Text(hdc, txt, &tr, tc, Ui_Font(UI_FONT_BODY),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    /* 右侧 V 形箭头 */
    int cx = rc.right - Ui_S(14);
    int cy = (rc.top + rc.bottom) / 2;
    int dy = Ui_S(2), dx = Ui_S(4);
    HPEN pen = CreatePen(PS_SOLID, Ui_S(1) < 1 ? 1 : Ui_S(1), on ? t->accent : t->text_sub);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, cx - dx, cy - dy, NULL);
    LineTo(hdc, cx, cy + dy);
    LineTo(hdc, cx + dx, cy - dy);
    SelectObject(hdc, old);
    DeleteObject(pen);

    EndPaint(h, &ps);
}

static void combo_open(HWND h) {
    ComboState* s = combo_state(h);
    if (!s || s->n <= 0) return;

    UiMenuItem items[UI_COMBO_MAX];
    memset(items, 0, sizeof(items));          /* dot 字段默认 0 = 不画点 */
    for (int i = 0; i < s->n; i++) {
        items[i].id = i + 1;
        items[i].label = s->items[i];
        if (s->colorize && s->dot[i] >= 0) items[i].dot = s->dot[i] + 1;
    }
    RECT r;
    GetWindowRect(h, &r);
    HWND root = GetAncestor(h, GA_ROOT);
    int cmd = Ui_PopMenuSel(root ? root : GetParent(h), r.left, r.bottom + Ui_S(2),
                            r.right - r.left, items, s->n, s->sel + 1);
    if (cmd > 0 && cmd <= s->n) {
        s->sel = cmd - 1;
        /* 重选同一个也要通知：预算对话框靠它把现有预算重新回填一次 */
        SendMessageW(GetParent(h), WM_COMMAND,
                     MAKEWPARAM((WORD)GetDlgCtrlID(h), CBN_SELCHANGE), (LPARAM)h);
    }
    SetFocus(h);
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK ComboProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    ComboState* s = combo_state(h);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        combo_paint(h, s);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_MOUSEMOVE:
        if (s && !s->hot) {
            s->hot = 1;
            InvalidateRect(h, NULL, FALSE);
        }
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (s && s->hot) {
            s->hot = 0;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(h);
        combo_open(h);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor((HCURSOR)(ULONG_PTR)IDC_HAND);
            return TRUE;
        }
        break;

    case WM_GETDLGCODE:
        /* 方向键留给自己，别被对话框的焦点导航吃掉 */
        return DLGC_WANTARROWS;

    case WM_KEYDOWN:
        if (!s) break;
        if (wp == VK_F4 || wp == VK_SPACE || wp == VK_RETURN) {
            combo_open(h);
            return 0;
        }
        if (wp == VK_DOWN || wp == VK_UP) {
            int d = (wp == VK_DOWN) ? 1 : -1;
            int i = s->sel + d;
            if (i < 0) i = s->n - 1;
            if (i >= s->n) i = 0;
            if (s->n > 0 && i != s->sel) {
                s->sel = i;
                SendMessageW(GetParent(h), WM_COMMAND,
                             MAKEWPARAM((WORD)GetDlgCtrlID(h), CBN_SELCHANGE), (LPARAM)h);
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        break;

    case WM_NCDESTROY:
        if (s) free(s);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int combo_register(HINSTANCE hInst) {
    if (g_combo_atom) return 1;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = ComboProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = COMBO_CLASS;
    /* CS_HREDRAW / CS_VREDRAW 省了，尺寸变化由父窗口 SetWindowPos 后自己重绘 */
    g_combo_atom = RegisterClassW(&wc);
    return g_combo_atom != 0;
}

HWND Ui_ComboCreate(HWND parent, int id, HINSTANCE hInst,
                    const char* const* utf8_items, int n) {
    if (!parent) return NULL;
    if (!hInst) hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(NULL);
    if (!combo_register(hInst)) return NULL;

    HWND h = CreateWindowExW(0, COMBO_CLASS, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, NULL);
    if (!h) return NULL;

    ComboState* s = (ComboState*)calloc(1, sizeof(ComboState));
    if (!s) { DestroyWindow(h); return NULL; }
    s->sel = -1;
    for (int i = 0; i < UI_COMBO_MAX; i++) s->dot[i] = -1;   /* calloc 的 0 是「蓝色」，不是「无色」 */
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);

    for (int i = 0; i < n && i < UI_COMBO_MAX && utf8_items; i++)
        Ui_ComboAddItem(h, utf8_items[i]);
    if (s->n > 0) s->sel = 0;
    return h;
}

void Ui_ComboAddItem(HWND h, const char* utf8_item) {
    ComboState* s = combo_state(h);
    if (!s || !utf8_item || !utf8_item[0] || s->n >= UI_COMBO_MAX) return;

    WCHAR w[32];
    utf8_to_wchar(utf8_item, w, 32);
    for (int i = 0; i < s->n; i++)
        if (wcscmp(s->items[i], w) == 0) return;      /* 去重 */
    wcsncpy(s->items[s->n], w, 31);
    s->items[s->n][31] = 0;
    s->dot[s->n] = combo_dot_of(s->items[s->n]);
    s->n++;
    InvalidateRect(h, NULL, FALSE);
}

void Ui_ComboSetColorize(HWND h, int on) {
    ComboState* s = combo_state(h);
    if (!s) return;
    s->colorize = on ? 1 : 0;
    for (int i = 0; i < s->n; i++) s->dot[i] = combo_dot_of(s->items[i]);
    InvalidateRect(h, NULL, FALSE);
}

void Ui_ComboSetItemDot(HWND h, int idx, int color_idx) {
    ComboState* s = combo_state(h);
    if (!s || idx < 0 || idx >= s->n) return;
    s->dot[idx] = (color_idx >= 0) ? color_idx : -1;
    s->colorize = 1;
    InvalidateRect(h, NULL, FALSE);
}

void Ui_ComboClear(HWND h) {
    ComboState* s = combo_state(h);
    if (!s) return;
    s->n = 0;
    s->sel = -1;
    InvalidateRect(h, NULL, FALSE);
}

void Ui_ComboSetSel(HWND h, int idx) {
    ComboState* s = combo_state(h);
    if (!s) return;
    if (idx < -1 || idx >= s->n) return;
    s->sel = idx;
    InvalidateRect(h, NULL, FALSE);
}

int Ui_ComboGetSel(HWND h) {
    ComboState* s = combo_state(h);
    return s ? s->sel : -1;
}

int Ui_ComboSetText(HWND h, const char* utf8_item) {
    ComboState* s = combo_state(h);
    if (!s || !utf8_item || !utf8_item[0]) return 0;
    WCHAR w[32];
    utf8_to_wchar(utf8_item, w, 32);
    for (int i = 0; i < s->n; i++) {
        if (wcscmp(s->items[i], w) == 0) {
            s->sel = i;
            InvalidateRect(h, NULL, FALSE);
            return 1;
        }
    }
    return 0;
}

int Ui_ComboGetText(HWND h, char* out, int cap) {
    ComboState* s = combo_state(h);
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!s || s->sel < 0 || s->sel >= s->n) return 0;
    wchar_to_utf8(s->items[s->sel], out, cap);
    return out[0] != 0;
}
