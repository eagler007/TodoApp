#include "trend.h"
#include "ui.h"
#include "theme.h"
#include "util.h"

#include <gdiplus/gdiplus.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define TREND_CLASS L"TodoTrendBox"

typedef struct {
    int    n;
    char   label[TREND_MAX][8];
    double v[TREND_MAX];
    int    hi;
} TrendData;

/* 折点几何：画和点命中判定必须用同一份坐标，所以抽出来共用 */
typedef struct {
    int    ok;
    int    n;
    int    step;                 /* 相邻点水平间距，n == 1 时为 0 */
    int    x0, x1, y0, y1;       /* 绘图区（不含轴标签边距） */
    double top;                  /* 纵轴上限 */
    POINT  pts[TREND_MAX];
} TrendGeom;

static TrendData g_td;
static HWND      g_trend_hw = NULL;
static int       g_trend_reg = 0;
static int       g_sel   = -1;   /* 点击选中的点；-1 = 跟着 hi 走 */
static int       g_hot   = -1;   /* 鼠标悬停的点 */
static int       g_track = 0;    /* 已挂 TME_LEAVE */

void Ui_TrendSetData(const char** labels, const double* vals, int n, int hi) {
    if (n < 0) n = 0;
    if (n > TREND_MAX) n = TREND_MAX;
    g_td.n  = n;
    g_td.hi = (hi >= 0 && hi < n) ? hi : -1;
    g_sel   = -1;                /* 换月份重新洗牌，选中回到默认 */
    g_hot   = -1;
    for (int i = 0; i < n; i++) {
        const char* s = (labels && labels[i]) ? labels[i] : "";
        strncpy(g_td.label[i], s, sizeof(g_td.label[0]) - 1);
        g_td.label[i][sizeof(g_td.label[0]) - 1] = 0;
        g_td.v[i] = (vals && vals[i] > 0) ? vals[i] : 0;
    }
    if (g_trend_hw && IsWindow(g_trend_hw))
        InvalidateRect(g_trend_hw, NULL, FALSE);
}

static ARGB argb_of(COLORREF c, BYTE a) {
    return ((ARGB)a << 24) | ((ARGB)GetRValue(c) << 16) |
           ((ARGB)GetGValue(c) << 8) | (ARGB)GetBValue(c);
}

/* 纵轴刻度：整根轴统一单位（上万就全部按「万」显示），别一半 5000 一半 1.0万 */
static void fmt_axis(double v, double top, char* out, int n) {
    if (top >= 10000.0) {
        if (v < 500.0) snprintf(out, n, "0");
        else           snprintf(out, n, "%.1f\xE4\xB8\x87", v / 10000.0);
    } else {
        snprintf(out, n, "%.0f", v);
    }
}

/* 金额：整数部分带千分位，dec = 0 或 2（气泡里精确到分） */
static void fmt_money(double v, int dec, char* out, int n) {
    int    neg = (v < 0);
    double a   = neg ? -v : v;
    char   raw[48];
    if (dec == 2) snprintf(raw, sizeof(raw), "%.2f", a);
    else          snprintf(raw, sizeof(raw), "%.0f", a);

    char ip[32], frac[8];
    frac[0] = 0;
    char* dot = strchr(raw, '.');
    if (dot) {
        size_t k = (size_t)(dot - raw);
        if (k > 31) k = 31;
        memcpy(ip, raw, k);
        ip[k] = 0;
        strncpy(frac, dot, sizeof(frac) - 1);
        frac[sizeof(frac) - 1] = 0;
    } else {
        strncpy(ip, raw, sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = 0;
    }

    char buf[48];
    int  j = 0, len = (int)strlen(ip);
    for (int i = 0; i < len && j < 40; i++) {
        if (i > 0 && (len - i) % 3 == 0) buf[j++] = ',';
        buf[j++] = ip[i];
    }
    buf[j] = 0;
    snprintf(out, n, "%s%s%s%s", neg ? "-" : "", "\xC2\xA5", buf, frac);
}

/* 刻度上限取整到 1/2/3/4/5/6/8/10 × 10^n：不然出现 3731 这种刻度很难看，
 * 且 top/2 必须也是整数（中值刻度要好看） */
static double nice_top(double mx) {
    if (mx <= 0) return 1;
    static const double muls[] = { 1, 2, 3, 4, 5, 6, 8, 10 };
    double v = mx * 1.04;
    double p = pow(10.0, floor(log10(v)));
    double n = v / p;
    for (int i = 0; i < (int)(sizeof(muls) / sizeof(muls[0])); i++)
        if (n <= muls[i] + 1e-9) return muls[i] * p;
    return 10.0 * p;
}

/* 折点坐标：画图和命中判定共用，避免两处算法漂移 */
static int calc_geom(int W, int H, TrendGeom* gm) {
    memset(gm, 0, sizeof(*gm));
    int pad_l = Ui_S(48), pad_r = Ui_S(16), pad_t = Ui_S(42), pad_b = Ui_S(24);
    gm->x0 = pad_l;
    gm->x1 = W - pad_r;
    gm->y0 = pad_t;
    gm->y1 = H - pad_b;
    if (gm->x1 - gm->x0 < 20 || gm->y1 - gm->y0 < 20) return 0;

    double mx = 0;
    for (int i = 0; i < g_td.n; i++) if (g_td.v[i] > mx) mx = g_td.v[i];
    gm->top = nice_top(mx);
    if (gm->top <= 0) gm->top = 1;

    int n = g_td.n;
    gm->n    = n;
    gm->step = (n > 1) ? (gm->x1 - gm->x0) / (n - 1) : 0;
    for (int i = 0; i < n; i++) {
        int px = (n > 1) ? (gm->x0 + gm->step * i) : ((gm->x0 + gm->x1) / 2);
        int py = gm->y1 - (int)((double)(gm->y1 - gm->y0) * (g_td.v[i] / gm->top) + 0.5);
        if (py < gm->y0) py = gm->y0;
        if (py > gm->y1) py = gm->y1;
        gm->pts[i].x = px;
        gm->pts[i].y = py;
    }
    gm->ok = 1;
    return 1;
}

/* 命中判定：横向给半格宽（点密也好点中），纵向给 22 逻辑像素的宽容；
 * 无数据的月份不响应，不然点在底边会冒出一堆 ¥0.00 */
static int hit_point(const TrendGeom* gm, int mx, int my) {
    if (!gm->ok || gm->n <= 0) return -1;
    int tolx = gm->step > 0 ? gm->step / 2 : Ui_S(24);
    if (tolx < Ui_S(12)) tolx = Ui_S(12);
    int toly = Ui_S(22);
    int best = -1, bd = 0x7FFFFFFF;
    for (int i = 0; i < gm->n; i++) {
        if (g_td.v[i] <= 0) continue;
        int dx = mx - gm->pts[i].x, dy = my - gm->pts[i].y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > tolx || dy > toly) continue;
        int d = dx * 4 + dy;          /* 同一列里挑纵向最近的那个点 */
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}


/* 折线 + 面积：GDI 的 Polyline 没有抗锯齿，线是台阶状的，所以曲线走 GDI+
 * flat API（mingw 的 C 语言下没有 GpGraphics 类封装）。
 * 控制点取水平切线（c1 = 起点右 1/3，c2 = 终点左 1/3），平滑且不会过冲。 */
static void paint_curve(HDC hdc, const POINT* pts, int n, int base_y,
                        COLORREF line_c, COLORREF fill_c) {
    if (n < 1) return;
    GpGraphics* gfx = NULL;
    if (GdipCreateFromHDC(hdc, &gfx) != Ok || !gfx) return;
    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);

    /* --- 面积 --- */
    GpPath* area = NULL;
    if (GdipCreatePath(FillModeAlternate, &area) == Ok && area) {
        for (int i = 0; i + 1 < n; i++) {
            REAL x0 = (REAL)pts[i].x,     y0 = (REAL)pts[i].y;
            REAL x1 = (REAL)pts[i + 1].x, y1 = (REAL)pts[i + 1].y;
            REAL dx = (x1 - x0) / 3.0f;
            GdipAddPathBezier(area, x0, y0, x0 + dx, y0, x1 - dx, y1, x1, y1);
        }
        if (n == 1) {
            REAL x0 = (REAL)pts[0].x, y0 = (REAL)pts[0].y;
            GdipAddPathLine(area, x0, y0, x0, y0);
        }
        GdipAddPathLine(area, (REAL)pts[n - 1].x, (REAL)base_y,
                              (REAL)pts[0].x,     (REAL)base_y);
        GdipClosePathFigure(area);

        GpSolidFill* br = NULL;
        if (GdipCreateSolidFill(argb_of(fill_c, 38), &br) == Ok && br) {
            GdipFillPath(gfx, br, area);
            GdipDeleteBrush(br);
        }
        GdipDeletePath(area);
    }

    /* --- 折线（不含底边，另建一条 path） --- */
    GpPath* line = NULL;
    if (GdipCreatePath(FillModeAlternate, &line) == Ok && line) {
        for (int i = 0; i + 1 < n; i++) {
            REAL x0 = (REAL)pts[i].x,     y0 = (REAL)pts[i].y;
            REAL x1 = (REAL)pts[i + 1].x, y1 = (REAL)pts[i + 1].y;
            REAL dx = (x1 - x0) / 3.0f;
            GdipAddPathBezier(line, x0, y0, x0 + dx, y0, x1 - dx, y1, x1, y1);
        }
        if (n == 1) {
            REAL x0 = (REAL)pts[0].x, y0 = (REAL)pts[0].y;
            GdipAddPathLine(line, x0, y0, x0, y0);
        }
        GpPen* pen = NULL;
        if (GdipCreatePen1(argb_of(line_c, 255), (REAL)(Ui_S(2) > 1 ? Ui_S(2) : 2),
                           UnitPixel, &pen) == Ok && pen) {
            GdipDrawPath(gfx, pen, line);
            GdipDeletePen(pen);
        }
        GdipDeletePath(line);
    }
    GdipDeleteGraphics(gfx);
}

/* 圆点：白/画布色描边把点和线分开；选中的点画大一号，悬停的点加一圈光晕 */
static void paint_dots(HDC hdc, const POINT* pts, int n, int act, int hot,
                       COLORREF line_c, COLORREF ring_c) {
    GpGraphics* gfx = NULL;
    if (GdipCreateFromHDC(hdc, &gfx) != Ok || !gfx) return;
    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);

    for (int i = 0; i < n; i++) {
        /* 没有数据的月份不画点：贴底的珠子看着像故障 */
        if (g_td.v[i] <= 0 && i != act) continue;
        int r = (i == act || i == hot) ? Ui_S(4) : Ui_S(3);
        if (r < 2) r = 2;
        REAL x = (REAL)pts[i].x, y = (REAL)pts[i].y;

        /* 悬停光晕（选中点不用，它本来就大） */
        if (i == hot && i != act) {
            int hr = r + Ui_S(5);
            GpSolidFill* bh = NULL;
            if (GdipCreateSolidFill(argb_of(line_c, 78), &bh) == Ok && bh) {
                GdipFillEllipse(gfx, bh, x - hr, y - hr, (REAL)(hr * 2), (REAL)(hr * 2));
                GdipDeleteBrush(bh);
            }
        }

        GpSolidFill* b1 = NULL;
        if (GdipCreateSolidFill(argb_of(ring_c, 255), &b1) == Ok && b1) {
            GdipFillEllipse(gfx, b1, x - r, y - r, (REAL)(r * 2), (REAL)(r * 2));
            GdipDeleteBrush(b1);
        }
        int rr = r - (i == act ? 0 : 1);
        if (rr < 1) rr = 1;
        GpSolidFill* b2 = NULL;
        if (GdipCreateSolidFill(argb_of(line_c, 255), &b2) == Ok && b2) {
            GdipFillEllipse(gfx, b2, x - rr, y - rr, (REAL)(rr * 2), (REAL)(rr * 2));
            GdipDeleteBrush(b2);
        }
    }
    GdipDeleteGraphics(gfx);
}

static void trend_paint(HWND hw, HDC hdc0) {
    const Theme* t = Theme_Now();
    RECT rc;
    GetClientRect(hw, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;

    /* 双缓冲：GDI+ 曲线 + GDI 文字混排，直接画屏会闪 */
    HDC     mdc = CreateCompatibleDC(hdc0);
    HBITMAP bmp = CreateCompatibleBitmap(hdc0, W, H);
    HBITMAP ob  = (HBITMAP)SelectObject(mdc, bmp);

    Ui_FillSolid(mdc, &rc, t->surface);

    /* --- 标题栏：左边标题，右边合计 --- */
    double sum = 0;
    for (int i = 0; i < g_td.n; i++) sum += g_td.v[i];

    RECT rt = { rc.left + Ui_S(14), rc.top + Ui_S(9),
                rc.right - Ui_S(14), rc.top + Ui_S(31) };
    Ui_Text(mdc, L"近 12 个月支出", &rt, t->text_sub, Ui_Font(UI_FONT_LABEL),
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (sum > 0) {
        char sb[64];
        fmt_money(sum, 0, sb, sizeof(sb));
        char tb[80];
        snprintf(tb, sizeof(tb), "\xE5\x90\x88\xE8\xAE\xA1 %s", sb);
        WCHAR ws[80];
        utf8_to_wchar(tb, ws, 80);
        Ui_Text(mdc, ws, &rt, t->text, Ui_Font(UI_FONT_SMALL),
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_td.n <= 0 || sum <= 0) {
        RECT er = { rc.left, rc.top + Ui_S(31), rc.right, rc.bottom };
        Ui_Text(mdc, L"暂无数据", &er, t->text_dim, Ui_Font(UI_FONT_SMALL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        BitBlt(hdc0, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, ob);
        DeleteObject(bmp);
        DeleteDC(mdc);
        return;
    }

    /* --- 绘图区：坐标统一由 calc_geom 给，和鼠标命中判定同一份 --- */
    TrendGeom gm;
    if (!calc_geom(W, H, &gm)) {
        SelectObject(mdc, ob); DeleteObject(bmp); DeleteDC(mdc);
        return;
    }
    const int x0 = gm.x0, x1 = gm.x1, y0 = gm.y0, y1 = gm.y1;
    const double top = gm.top;
    const POINT* pts = gm.pts;
    const int n = gm.n, step = gm.step;

    /* 横向网格 + 纵轴刻度 */
    HPEN old_pen = NULL, grid_pen = CreatePen(PS_SOLID, 1, t->border_soft);
    old_pen = (HPEN)SelectObject(mdc, grid_pen);
    for (int k = 0; k <= 2; k++) {
        int y = y1 - (y1 - y0) * k / 2;
        MoveToEx(mdc, x0, y, NULL);
        LineTo(mdc, x1, y);
    }
    SelectObject(mdc, old_pen);
    DeleteObject(grid_pen);

    for (int k = 0; k <= 2; k++) {
        int y = y1 - (y1 - y0) * k / 2;
        char lab[24];
        fmt_axis(top * k / 2.0, top, lab, sizeof(lab));
        WCHAR wl[32];
        utf8_to_wchar(lab, wl, 32);
        RECT lr = { rc.left + Ui_S(2), y - Ui_S(8), x0 - Ui_S(6), y + Ui_S(8) };
        Ui_Text(mdc, wl, &lr, t->text_dim, Ui_Font(UI_FONT_SMALL),
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 当前标注点：点过就用点的，没点过用调用方指定的 hi（默认是所选月） */
    int act = (g_sel >= 0 && g_sel < n) ? g_sel : g_td.hi;
    if (act >= n) act = -1;

    paint_curve(mdc, pts, n, y1, t->accent, t->accent);
    paint_dots(mdc, pts, n, act, g_hot, t->accent, t->surface);

    /* 横轴月份标签 */
    for (int i = 0; i < n; i++) {
        WCHAR wl[16];
        utf8_to_wchar(g_td.label[i], wl, 16);
        COLORREF c = (i == act) ? t->text : t->text_dim;
        RECT lr = { pts[i].x - step / 2, y1 + Ui_S(4),
                    pts[i].x + step / 2, y1 + Ui_S(20) };
        if (n == 1) { lr.left = pts[i].x - Ui_S(20); lr.right = pts[i].x + Ui_S(20); }
        Ui_Text(mdc, wl, &lr, c,
                (i == act) ? Ui_Font(UI_FONT_LABEL) : Ui_Font(UI_FONT_SMALL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* 标注点的金额气泡：带上月份，点哪个就显示哪个；贴点的左上，出界就内收 */
    if (act >= 0) {
        char mb[64], tb[96];
        fmt_money(g_td.v[act], 2, mb, sizeof(mb));
        snprintf(tb, sizeof(tb), "%s\xE6\x9C\x88 %s", g_td.label[act], mb);
        WCHAR wm[96];
        utf8_to_wchar(tb, wm, 96);

        HFONT of = (HFONT)SelectObject(mdc, Ui_Font(UI_FONT_SMALL));
        SIZE  sz = { 0, 0 };
        GetTextExtentPoint32W(mdc, wm, (int)wcslen(wm), &sz);
        SelectObject(mdc, of);

        int ph = Ui_S(21);
        int pw = sz.cx + Ui_S(20);
        if (pw > W - Ui_S(16)) pw = W - Ui_S(16);
        int bx = pts[act].x - pw + Ui_S(10);
        int by = pts[act].y - Ui_S(14) - ph;
        if (by < gm.y0) by = pts[act].y + Ui_S(11);
        if (bx + pw > rc.right - Ui_S(4)) bx = rc.right - Ui_S(4) - pw;
        if (bx < rc.left + Ui_S(4)) bx = rc.left + Ui_S(4);
        RECT br = { bx, by, bx + pw, by + ph };
        Ui_FillRound(mdc, &br, ph / 2, t->accent_soft);
        Ui_Text(mdc, wm, &br, t->accent_soft_fg, Ui_Font(UI_FONT_SMALL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(hdc0, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

static LRESULT CALLBACK trend_proc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        trend_paint(hw, hdc);
        EndPaint(hw, &ps);
        return 0;
    }
    /* 鼠标移动：更新悬停点（只在下标变化时重绘，免得一路刷屏） */
    case WM_MOUSEMOVE: {
        RECT rc;
        GetClientRect(hw, &rc);
        TrendGeom gm;
        int hot = calc_geom(rc.right, rc.bottom, &gm)
                    ? hit_point(&gm, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp))
                    : -1;
        if (hot != g_hot) {
            g_hot = hot;
            InvalidateRect(hw, NULL, FALSE);
        }
        if (!g_track) {
            TRACKMOUSEEVENT tme;
            tme.cbSize      = sizeof(tme);
            tme.dwFlags     = TME_LEAVE;
            tme.hwndTrack   = hw;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            g_track = 1;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_track = 0;
        if (g_hot != -1) {
            g_hot = -1;
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;
    /* 点中折点 → 选中它并显示金额；点空白 → 取消，回到默认标注 */
    case WM_LBUTTONDOWN: {
        RECT rc;
        GetClientRect(hw, &rc);
        TrendGeom gm;
        int idx = calc_geom(rc.right, rc.bottom, &gm)
                    ? hit_point(&gm, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp))
                    : -1;
        if (idx != g_sel) {
            g_sel = idx;
            InvalidateRect(hw, NULL, FALSE);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(g_hot >= 0 ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        break;
    }
    (void)wp; (void)lp;
    return DefWindowProcW(hw, msg, wp, lp);
}

static int trend_register(HINSTANCE hInst) {
    if (g_trend_reg) return 1;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = trend_proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = TREND_CLASS;
    if (!RegisterClassW(&wc)) return 0;
    g_trend_reg = 1;
    return 1;
}

HWND Ui_TrendCreate(HWND parent, int id, int x, int y, int w, int h) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(NULL);
    if (!trend_register(hInst)) return NULL;

    HWND hw = CreateWindowExW(0, TREND_CLASS, L"", WS_CHILD | WS_VISIBLE,
                              Ui_S(x), Ui_S(y), Ui_S(w), Ui_S(h),
                              parent, (HMENU)(INT_PTR)id, hInst, NULL);
    g_trend_hw = hw;
    return hw;
}
