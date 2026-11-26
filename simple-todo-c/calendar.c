#include "calendar.h"
#include "ui.h"
#include "theme.h"
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef CS_DROPSHADOW
#define CS_DROPSHADOW 0x00020000
#endif

#define CAL_CLASS L"TodoCalendar"

#define CAL_W     248
#define CAL_H     272
#define CAL_MH    200
#define CAL_TW    212
#define CAL_TH    250
#define CAL_PAD   12
#define CAL_TITLE 28
#define CAL_WD    20
#define CAL_TROWS 7     /* 时间选择器每列可见行数 */

typedef struct {
    int  y, m;              /* 当前显示年月 */
    int  sel_y, sel_m, sel_d;
    int  hover;             /* 日期格 0..41；CAL_HOV_PREV / CAL_HOV_NEXT */
    int  mode;              /* 0 = 选日期，1 = 只选年月，2 = 选时间 */
    int  done;
    int  result;
    WCHAR out[16];
    /* 时间模式 */
    int  hh, mm;
    int  hscroll, mscroll;  /* 两列可见窗口的起始索引 */
    int  hcol;              /* 悬停列 0=时 1=分，-1 无 */
    int  hidx;              /* 悬停行 0..CAL_TROWS-1，-1 无 */
} CalState;

#define CAL_HOV_NONE  -1
#define CAL_HOV_PREV  -2
#define CAL_HOV_NEXT  -3

typedef struct {
    RECT rc;
    RECT prev, next, title;
    RECT wd[7];
    RECT day[42];
    RECT mon[12];
    RECT thead[2];              /* 「时」「分」表头 */
    RECT tup[2], tdn[2];        /* 两列上下滚动箭头 */
    RECT trow[2][CAL_TROWS];    /* 两列可见行 */
} CalLayout;

static ATOM g_cal_atom = 0;

static int days_in_month(int y, int m) {
    static const int d[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2) {
        int leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[m - 1];
}

/* 0 = 周日 */
static int weekday_of(int y, int m, int d) {
    static const int t[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
    if (m < 3) y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
    return (w + 7) % 7;
}

static void parse_date(const WCHAR* s, int* y, int* m, int* d) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    *y = st.wYear; *m = st.wMonth; *d = st.wDay;
    if (!s || !*s) return;
    int a = 0, b = 0, c = 0;
    if (swscanf(s, L"%d/%d/%d", &a, &b, &c) == 3 ||
        swscanf(s, L"%d-%d-%d", &a, &b, &c) == 3) {
        if (a > 1900 && b >= 1 && b <= 12 && c >= 1 && c <= 31) {
            *y = a; *m = b; *d = c;
        }
    }
}

static void cal_layout(HWND h, CalState* s, CalLayout* L) {
    RECT rc;
    GetClientRect(h, &rc);
    L->rc = rc;

    int pad = Ui_S(CAL_PAD);
    int ty = pad;
    int th = Ui_S(CAL_TITLE);
    int aw = Ui_S(24);

    L->prev.left = pad;
    L->prev.right = pad + aw;
    L->next.right = rc.right - pad;
    L->next.left = rc.right - pad - aw;
    L->prev.top = L->next.top = ty;
    L->prev.bottom = L->next.bottom = ty + th;

    L->title.left = L->prev.right;
    L->title.right = L->next.left;
    L->title.top = ty;
    L->title.bottom = ty + th;

    int y = ty + th + Ui_S(2);
    int gridw = rc.right - pad * 2;

    if (s && s->mode == 2) {
        /* 时间模式：左右两列（时 / 分），各带上下滚动箭头 */
        int gap = Ui_S(10);
        int inner = rc.right - pad * 2;
        int cw = (inner - gap) / 2;
        int hh = Ui_S(18);
        int aw = Ui_S(14);
        int top = pad;
        for (int c = 0; c < 2; c++) {
            int lx = pad + (cw + gap) * c;
            L->thead[c].left = lx;
            L->thead[c].right = lx + cw;
            L->thead[c].top = top;
            L->thead[c].bottom = top + hh;

            L->tup[c].left = lx;
            L->tup[c].right = lx + cw;
            L->tup[c].top = top + hh;
            L->tup[c].bottom = L->tup[c].top + aw;

            L->tdn[c].left = lx;
            L->tdn[c].right = lx + cw;
            L->tdn[c].bottom = rc.bottom - pad;
            L->tdn[c].top = L->tdn[c].bottom - aw;

            int ly0 = L->tup[c].bottom;
            int ly1 = L->tdn[c].top;
            int rh = (ly1 - ly0) / CAL_TROWS;
            for (int r = 0; r < CAL_TROWS; r++) {
                RECT* rr = &L->trow[c][r];
                rr->left = lx;
                rr->right = lx + cw;
                rr->top = ly0 + rh * r;
                rr->bottom = rr->top + rh;
            }
        }
        return;
    }

    if (s && s->mode) {
        /* 月份模式：4 行 x 3 列 */
        int cw = gridw / 3;
        int ch = (rc.bottom - pad - y) / 4;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 3; c++) {
                RECT* m = &L->mon[r * 3 + c];
                m->left = pad + cw * c;
                m->right = m->left + cw;
                m->top = y + ch * r;
                m->bottom = m->top + ch;
            }
        }
        return;
    }

    int cw = gridw / 7;
    int wdh = Ui_S(CAL_WD);
    for (int i = 0; i < 7; i++) {
        L->wd[i].left = pad + cw * i;
        L->wd[i].right = L->wd[i].left + cw;
        L->wd[i].top = y;
        L->wd[i].bottom = y + wdh;
    }
    y += wdh + Ui_S(2);

    int ch = (rc.bottom - pad - y) / 6;
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 7; c++) {
            RECT* d = &L->day[r * 7 + c];
            d->left = pad + cw * c;
            d->right = d->left + cw;
            d->top = y + ch * r;
            d->bottom = d->top + ch;
        }
    }
}

/* 第 idx 个格子对应的日号；非本月返回 0 */
/* 表格按「周一」起头，weekday_of 是「周日」起头，这里做偏移 */
static int first_col(int y, int m) {
    return (weekday_of(y, m, 1) + 6) % 7;
}

static int cell_day(const CalState* s, int idx) {
    int first = first_col(s->y, s->m);
    int dim = days_in_month(s->y, s->m);
    int day = idx - first + 1;
    if (day < 1 || day > dim) return 0;
    return day;
}

static void draw_arrow(HDC hdc, const RECT* rc, int dir, COLORREF c) {
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int s = Ui_S(4);
    POINT p[3];
    if (dir < 0) {
        p[0].x = cx + s / 2; p[0].y = cy - s;
        p[1].x = cx + s / 2; p[1].y = cy + s;
        p[2].x = cx - s;     p[2].y = cy;
    } else {
        p[0].x = cx - s / 2; p[0].y = cy - s;
        p[1].x = cx - s / 2; p[1].y = cy + s;
        p[2].x = cx + s;     p[2].y = cy;
    }
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    Polygon(hdc, p, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void draw_tri_v(HDC hdc, const RECT* rc, int dir, COLORREF c) {
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int s = Ui_S(4);
    POINT p[3];
    if (dir < 0) {
        p[0].x = cx - s; p[0].y = cy + s / 2;
        p[1].x = cx + s; p[1].y = cy + s / 2;
        p[2].x = cx;     p[2].y = cy - s / 2;
    } else {
        p[0].x = cx - s; p[0].y = cy - s / 2;
        p[1].x = cx + s; p[1].y = cy - s / 2;
        p[2].x = cx;     p[2].y = cy + s / 2;
    }
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    Polygon(hdc, p, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void cal_paint(HWND hw, HDC hdc0) {
    CalState* s = (CalState*)GetWindowLongPtrW(hw, GWLP_USERDATA);
    if (!s) return;
    const Theme* t = Theme_Now();

    CalLayout L;
    cal_layout(hw, s, &L);

    HDC hdc = CreateCompatibleDC(hdc0);
    HBITMAP bmp = CreateCompatibleBitmap(hdc0, L.rc.right, L.rc.bottom);
    HBITMAP ob = (HBITMAP)SelectObject(hdc, bmp);

    Ui_FillSolid(hdc, &L.rc, t->surface);

    SYSTEMTIME now;
    GetLocalTime(&now);

    if (s->mode == 2) {
        static const WCHAR* hd[2] = { L"时", L"分" };
        for (int c = 0; c < 2; c++) {
            Ui_Text(hdc, hd[c], &L.thead[c], t->text_dim, Ui_Font(UI_FONT_SMALL),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            draw_tri_v(hdc, &L.tup[c], -1, t->text_sub);
            draw_tri_v(hdc, &L.tdn[c], 1, t->text_sub);
        }
        for (int c = 0; c < 2; c++) {
            int cnt  = c ? 60 : 24;
            int base = c ? s->mscroll : s->hscroll;
            int sel  = c ? s->mm : s->hh;
            for (int r = 0; r < CAL_TROWS; r++) {
                int v = base + r;
                if (v >= cnt) break;
                RECT cc = L.trow[c][r];
                cc.left  += Ui_S(5);
                cc.right -= Ui_S(5);
                cc.top   += Ui_S(1);
                cc.bottom -= Ui_S(1);
                int is_sel = (v == sel);
                int is_hov = (s->hcol == c && s->hidx == r);
                WCHAR buf[8];
                swprintf(buf, 8, L"%02d", v);
                if (is_sel) {
                    Ui_FillRound(hdc, &cc, Ui_S(5), t->accent);
                    Ui_Text(hdc, buf, &cc, t->accent_fg, Ui_Font(UI_FONT_SMALL),
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    if (is_hov) Ui_FillRound(hdc, &cc, Ui_S(5), t->hover_bg);
                    Ui_Text(hdc, buf, &cc, t->text, Ui_Font(UI_FONT_SMALL),
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }
        Ui_StrokeRound(hdc, &L.rc, Ui_S(8), t->border, 1.0f);

        BitBlt(hdc0, 0, 0, L.rc.right, L.rc.bottom, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, ob);
        DeleteObject(bmp);
        DeleteDC(hdc);
        return;
    }

    /* 标题：日期模式「2026 年 10 月」，月份模式只显示年 */
    {
        WCHAR buf[32];
        if (s->mode) swprintf(buf, 32, L"%d 年", s->y);
        else         swprintf(buf, 32, L"%d 年 %d 月", s->y, s->m);
        Ui_Text(hdc, buf, &L.title, t->text, Ui_Font(UI_FONT_BODY),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 左右切月（月份模式下是切年） */
    if (s->hover == CAL_HOV_PREV)
        Ui_FillRound(hdc, &L.prev, Ui_S(5), t->hover_bg);
    if (s->hover == CAL_HOV_NEXT)
        Ui_FillRound(hdc, &L.next, Ui_S(5), t->hover_bg);
    draw_arrow(hdc, &L.prev, -1, t->text_sub);
    draw_arrow(hdc, &L.next, 1, t->text_sub);

    if (s->mode) {
        for (int i = 0; i < 12; i++) {
            RECT cell = L.mon[i];
            int is_sel = (s->y == s->sel_y && (i + 1) == s->sel_m);
            int is_cur = (s->y == (int)now.wYear && (i + 1) == (int)now.wMonth);

            int mw = cell.right - cell.left - Ui_S(12);
            if (mw < Ui_S(20)) mw = Ui_S(20);
            int mh = Ui_S(28);
            RECT cc;
            cc.left = (cell.left + cell.right) / 2 - mw / 2;
            cc.right = cc.left + mw;
            cc.top = (cell.top + cell.bottom) / 2 - mh / 2;
            cc.bottom = cc.top + mh;

            WCHAR mt[16];
            swprintf(mt, 16, L"%d 月", i + 1);

            if (is_sel) {
                Ui_FillRound(hdc, &cc, Ui_S(6), t->accent);
                Ui_Text(hdc, mt, &cc, t->accent_fg, Ui_Font(UI_FONT_SMALL),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                if (s->hover == i) Ui_FillRound(hdc, &cc, Ui_S(6), t->hover_bg);
                if (is_cur) {
                    Ui_StrokeRound(hdc, &cc, Ui_S(6), t->accent, 1.0f);
                    Ui_Text(hdc, mt, &cc, t->accent, Ui_Font(UI_FONT_SMALL),
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    Ui_Text(hdc, mt, &cc, t->text, Ui_Font(UI_FONT_SMALL),
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }
        Ui_StrokeRound(hdc, &L.rc, Ui_S(8), t->border, 1.0f);

        BitBlt(hdc0, 0, 0, L.rc.right, L.rc.bottom, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, ob);
        DeleteObject(bmp);
        DeleteDC(hdc);
        return;
    }

    /* 星期表头，周日在最后（中式习惯周一开头：一 二 三 四 五 六 日） */
    {
        static const WCHAR* wd[7] = { L"一", L"二", L"三", L"四", L"五", L"六", L"日" };
        for (int i = 0; i < 7; i++) {
            Ui_Text(hdc, wd[i], &L.wd[i], t->text_dim, Ui_Font(UI_FONT_SMALL),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    for (int i = 0; i < 42; i++) {
        int day = cell_day(s, i);
        if (!day) continue;
        RECT cell = L.day[i];

        int is_sel = (s->y == s->sel_y && s->m == s->sel_m && day == s->sel_d);
        int is_today = (s->y == (int)now.wYear && s->m == (int)now.wMonth && day == (int)now.wDay);

        int sz = Ui_S(26);
        RECT cc;
        cc.left = (cell.left + cell.right) / 2 - sz / 2;
        cc.right = cc.left + sz;
        cc.top = (cell.top + cell.bottom) / 2 - sz / 2;
        cc.bottom = cc.top + sz;

        if (is_sel) {
            Ui_FillRound(hdc, &cc, sz / 2, t->accent);
            WCHAR d[8];
            swprintf(d, 8, L"%d", day);
            Ui_Text(hdc, d, &cc, t->accent_fg, Ui_Font(UI_FONT_SMALL),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            if (s->hover == i) Ui_FillRound(hdc, &cc, sz / 2, t->hover_bg);
            WCHAR d[8];
            swprintf(d, 8, L"%d", day);
            if (is_today) {
                Ui_StrokeRound(hdc, &cc, sz / 2, t->accent, 1.0f);
                Ui_Text(hdc, d, &cc, t->accent, Ui_Font(UI_FONT_SMALL),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                Ui_Text(hdc, d, &cc, t->text, Ui_Font(UI_FONT_SMALL),
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
    /* 外框压在最上层 */
    Ui_StrokeRound(hdc, &L.rc, Ui_S(8), t->border, 1.0f);

    BitBlt(hdc0, 0, 0, L.rc.right, L.rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, ob);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

static int hit_test(const CalLayout* L, const CalState* s, POINT p) {
    if (PtInRect(&L->prev, p)) return CAL_HOV_PREV;
    if (PtInRect(&L->next, p)) return CAL_HOV_NEXT;
    if (s && s->mode) {
        for (int i = 0; i < 12; i++) {
            if (PtInRect(&L->mon[i], p)) return i;
        }
    } else {
        for (int i = 0; i < 42; i++) {
            if (PtInRect(&L->day[i], p)) return i;
        }
    }
    return CAL_HOV_NONE;
}

/* 时间模式命中：返回 0=行 1=上箭头 2=下箭头 3=无；*col 列，*idx 行 */
static int time_hit(const CalLayout* L, POINT p, int* col, int* idx) {
    for (int c = 0; c < 2; c++) {
        if (PtInRect(&L->tup[c], p)) { *col = c; return 1; }
        if (PtInRect(&L->tdn[c], p)) { *col = c; return 2; }
        for (int r = 0; r < CAL_TROWS; r++) {
            if (PtInRect(&L->trow[c][r], p)) { *col = c; *idx = r; return 0; }
        }
    }
    return 3;
}

static void time_scroll(CalState* s, int col, int delta) {
    int* sc  = col ? &s->mscroll : &s->hscroll;
    int  max = (col ? 60 : 24) - CAL_TROWS;
    *sc += delta;
    if (*sc < 0) *sc = 0;
    if (*sc > max) *sc = max;
}

static void pick_time(CalState* s) {
    swprintf(s->out, 16, L"%02d:%02d", s->hh, s->mm);
    s->result = 1;
    s->done = 1;
}

static void step_month(CalState* s, int delta) {
    s->m += delta;
    if (s->m < 1)  { s->m = 12; s->y -= 1; }
    if (s->m > 12) { s->m = 1;  s->y += 1; }
}

static void pick(CalState* s, int day) {
    s->sel_y = s->y;
    s->sel_m = s->m;
    s->sel_d = day;
    swprintf(s->out, 16, L"%04d-%02d-%02d", s->y, s->m, day);
    s->result = 1;
    s->done = 1;
}

static void pick_month(CalState* s, int mon) {
    s->sel_y = s->y;
    s->sel_m = mon + 1;
    swprintf(s->out, 16, L"%04d-%02d", s->y, mon + 1);
    s->result = 1;
    s->done = 1;
}

static LRESULT CALLBACK CalProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    CalState* s = (CalState*)GetWindowLongPtrW(hw, GWLP_USERDATA);
    CalLayout L;
    if (s) cal_layout(hw, s, &L);

    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        cal_paint(hw, hdc);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (!s) break;
        if (s->mode == 2) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int col = -1, idx = -1;
            int k = time_hit(&L, p, &col, &idx);
            int hc = (k == 0) ? col : -1;
            int hi = (k == 0) ? idx : -1;
            if (hc != s->hcol || hi != s->hidx) {
                s->hcol = hc; s->hidx = hi;
                InvalidateRect(hw, NULL, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }
        {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int h = hit_test(&L, s, p);
            if (h != s->hover) { s->hover = h; InvalidateRect(hw, NULL, FALSE); }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (!s || s->mode != 2) break;
        {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hw, &p);
            int col = (p.x < (L.rc.right / 2)) ? 0 : 1;
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            time_scroll(s, col, d > 0 ? -1 : 1);
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (s && s->hover != CAL_HOV_NONE) {
            s->hover = CAL_HOV_NONE;
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        if (!s) break;
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (!PtInRect(&L.rc, p)) { s->done = 1; s->result = 0; return 0; }

        if (s->mode == 2) {
            int col = -1, idx = -1;
            int k = time_hit(&L, p, &col, &idx);
            if (k == 1) { time_scroll(s, col, -1); InvalidateRect(hw, NULL, FALSE); }
            else if (k == 2) { time_scroll(s, col, 1); InvalidateRect(hw, NULL, FALSE); }
            else if (k == 0) {
                int base = col ? s->mscroll : s->hscroll;
                int v = base + idx;
                if (col == 0) {
                    if (v < 24) s->hh = v;
                    InvalidateRect(hw, NULL, FALSE);
                } else {
                    if (v < 60) s->mm = v;
                    pick_time(s);
                }
            }
            return 0;
        }

        int h = hit_test(&L, s, p);
        if (h == CAL_HOV_PREV) { step_month(s, -1); InvalidateRect(hw, NULL, FALSE); }
        else if (h == CAL_HOV_NEXT) { step_month(s, 1); InvalidateRect(hw, NULL, FALSE); }
        else if (h >= 0) {
            if (s->mode) {
                pick_month(s, h);
            } else {
                int day = cell_day(s, h);
                if (day) pick(s, day);
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (s) { s->done = 1; s->result = 0; }
        return 0;

    case WM_KEYDOWN:
        if (!s) break;
        if (wp == VK_ESCAPE) { s->done = 1; s->result = 0; return 0; }
        if (wp == VK_RETURN) {
            if (s->mode == 2)    pick_time(s);
            else if (s->mode)    pick_month(s, s->sel_m - 1);
            else                 pick(s, s->sel_d);
            return 0;
        }
        if (s->mode == 2) break;
        if (wp == VK_LEFT)  { step_month(s, -1); InvalidateRect(hw, NULL, FALSE); return 0; }
        if (wp == VK_RIGHT) { step_month(s, 1);  InvalidateRect(hw, NULL, FALSE); return 0; }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && s && !s->done) { s->done = 1; s->result = 0; }
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static int cal_register(HINSTANCE hInst) {
    if (g_cal_atom) return 1;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = CalProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CAL_CLASS;
    wc.style = CS_DROPSHADOW;
    g_cal_atom = RegisterClassW(&wc);
    return g_cal_atom != 0;
}

/* "2026-10" / "2026/10" / "2026-10-23" 都取年月 */
static void parse_ym(const WCHAR* s, int* y, int* m) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    *y = st.wYear; *m = st.wMonth;
    if (!s || !*s) return;
    int a = 0, b = 0, c = 0;
    int n = swscanf(s, L"%d/%d/%d", &a, &b, &c);
    if (n < 2) n = swscanf(s, L"%d-%d-%d", &a, &b, &c);
    if (n >= 2 && a > 1900 && b >= 1 && b <= 12) { *y = a; *m = b; }
}

/* "10:53" / "9:05" 取时分 */
static void parse_hm(const WCHAR* s, int* hh, int* mm) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    *hh = st.wHour; *mm = st.wMinute;
    if (!s || !*s) return;
    int a = -1, b = -1;
    if (swscanf(s, L"%d:%d", &a, &b) == 2 && a >= 0 && a < 24 && b >= 0 && b < 60) {
        *hh = a; *mm = b;
    }
}

static int pick_core(HWND owner, HWND anchor, int px, int py,
                     const WCHAR* cur, int mode, WCHAR* out, int out_n) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(NULL);
    if (!cal_register(hInst)) return 0;

    CalState s;
    memset(&s, 0, sizeof(s));
    s.hover = CAL_HOV_NONE;
    s.hcol = -1;
    s.hidx = -1;
    s.mode = mode;
    if (mode == 2) {
        parse_hm(cur, &s.hh, &s.mm);
        s.hscroll = s.hh - CAL_TROWS / 2;
        if (s.hscroll < 0) s.hscroll = 0;
        if (s.hscroll > 24 - CAL_TROWS) s.hscroll = 24 - CAL_TROWS;
        s.mscroll = s.mm - CAL_TROWS / 2;
        if (s.mscroll < 0) s.mscroll = 0;
        if (s.mscroll > 60 - CAL_TROWS) s.mscroll = 60 - CAL_TROWS;
    } else if (mode) {
        parse_ym(cur, &s.y, &s.m);
        s.sel_y = s.y;
        s.sel_m = s.m;
    } else {
        parse_date(cur, &s.y, &s.m, &s.sel_d);
        s.sel_y = s.y;
        s.sel_m = s.m;
        if (s.sel_d > days_in_month(s.y, s.m)) s.sel_d = 1;
    }

    int W = Ui_S(mode == 2 ? CAL_TW : CAL_W);
    int H = Ui_S(mode == 2 ? CAL_TH : (mode ? CAL_MH : CAL_H));
    int x, y;
    if (anchor) {
        RECT ar;
        GetWindowRect(anchor, &ar);
        x = ar.left;
        y = ar.bottom + Ui_S(4);
    } else {
        x = px;
        y = py;
    }
    /* 贴边时回收 */
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (x + W > wa.right) x = wa.right - W;
    if (y + H > wa.bottom) {
        if (anchor) {
            RECT ar;
            GetWindowRect(anchor, &ar);
            y = ar.top - H - Ui_S(4);
        }
        if (y < wa.top) y = wa.top;
    }
    if (x < wa.left) x = wa.left;

    HWND hw = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CAL_CLASS, L"",
                              WS_POPUP, x, y, W, H, owner, NULL, hInst, NULL);
    if (!hw) return 0;

    SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)&s);
    ShowWindow(hw, SW_SHOW);
    SetFocus(hw);

    MSG msg;
    while (!s.done && GetMessageW(&msg, NULL, 0, 0)) {
        /* 主窗口被关闭等情况 */
        if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindow(hw);

    if (s.result && out && out_n > 0) {
        wcsncpy(out, s.out, (size_t)out_n - 1);
        out[out_n - 1] = 0;
    }
    return s.result;
}

int Ui_PickDate(HWND owner, HWND anchor, int px, int py,
                const WCHAR* cur, WCHAR* out, int out_n) {
    return pick_core(owner, anchor, px, py, cur, 0, out, out_n);
}

int Ui_PickMonth(HWND owner, HWND anchor, int px, int py,
                 const WCHAR* cur, WCHAR* out, int out_n) {
    return pick_core(owner, anchor, px, py, cur, 1, out, out_n);
}

int Ui_PickTime(HWND owner, HWND anchor, int px, int py,
                const WCHAR* cur, WCHAR* out, int out_n) {
    return pick_core(owner, anchor, px, py, cur, 2, out, out_n);
}
