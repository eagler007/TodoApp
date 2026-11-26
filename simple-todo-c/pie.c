#include "pie.h"
#include "ui.h"
#include "theme.h"
#include "util.h"

#include <gdiplus/gdiplus.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define PIE_CLASS L"TodoPieBox"
#define PIE_PI    3.14159265358979323846

typedef struct {
    int    n;
    char   name[PIE_MAX][32];
    double pct[PIE_MAX];
    int    ci[PIE_MAX];
    double total;
} PieData;

static PieData g_pie;
static HWND    g_pie_hw = NULL;
static int     g_pie_reg = 0;

void Ui_PieSetData(const char** names, const double* pct, const int* ci,
                   int n, double total) {
    if (n < 0) n = 0;
    if (n > PIE_MAX) n = PIE_MAX;
    g_pie.n = n;
    g_pie.total = total;
    for (int i = 0; i < n; i++) {
        const char* s = (names && names[i]) ? names[i] : "";
        strncpy(g_pie.name[i], s, sizeof(g_pie.name[0]) - 1);
        g_pie.name[i][sizeof(g_pie.name[0]) - 1] = 0;
        g_pie.pct[i] = (pct && pct[i] > 0) ? pct[i] : 0;
        g_pie.ci[i]  = ci ? ci[i] : 0;
    }
    if (g_pie_hw && IsWindow(g_pie_hw))
        InvalidateRect(g_pie_hw, NULL, FALSE);
}

/* 扇区填充色：图表专用色板（500 档明度），比 chip 的浅底深字更"实" */
static COLORREF slice_color(int ci) {
    COLORREF c = RGB(0x3B, 0x82, 0xF6);
    Ui_CatChartColor(ci, &c);
    return c;
}

static ARGB argb_of(COLORREF c, BYTE a) {
    return ((ARGB)a << 24) | ((ARGB)GetRValue(c) << 16) |
           ((ARGB)GetGValue(c) << 8) | (ARGB)GetBValue(c);
}

/* 用 GDI+ 画圆环：GDI 的 Pie/Ellipse 没有抗锯齿，边缘是一格一格的台阶。
 * mingw 的 gdiplus 在 C 语言下只有 flat API（GdipXxx），没有 C++ 类封装。
 * 这里先在内存 DC 上合成整张图（双缓冲），再一次性 BitBlt 到窗口。 */
static void paint_donut(HDC hdc, int cx, int cy, int R, int ir, const Theme* t) {
    GpGraphics* gfx = NULL;
    if (GdipCreateFromHDC(hdc, &gfx) != Ok || !gfx) return;
    GdipSetSmoothingMode(gfx, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(gfx, PixelOffsetModeHalf);

    REAL box  = (REAL)(R * 2);
    REAL left = (REAL)(cx - R);
    REAL top  = (REAL)(cy - R);
    ARGB seam_argb = argb_of(t->surface, 255);

    /* 缝隙色 = 画布底色：扇区之间靠这 2px 描边分开，比硬拼接干净 */
    int        seam_px = Ui_S(2) > 1 ? Ui_S(2) : 1;
    GpPen*     seam    = NULL;
    GdipCreatePen1(seam_argb, (REAL)seam_px, UnitPixel, &seam);

    /* GDI+ 的角度单位是「度」，0 度在 3 点钟方向、顺时针递增；
     * 我们的累加角从 12 点开始，所以起始角统一减 90。 */
    double ang = 0.0;
    for (int i = 0; i < g_pie.n; i++) {
        double sweep = g_pie.pct[i] / 100.0 * 360.0;
        if (sweep <= 0.01) continue;
        REAL a1 = (REAL)(ang - 90.0);
        ang += sweep;

        GpPath* p = NULL;
        if (GdipCreatePath(FillModeAlternate, &p) != Ok || !p) continue;
        GdipAddPathPie(p, left, top, box, box, a1, (REAL)sweep);

        GpSolidFill* br = NULL;
        if (GdipCreateSolidFill(argb_of(slice_color(g_pie.ci[i]), 255), &br) == Ok && br) {
            GdipFillPath(gfx, br, p);
            GdipDeleteBrush(br);
        }
        if (seam) GdipDrawPath(gfx, seam, p);
        GdipDeletePath(p);
    }

    if (seam) GdipDeletePen(seam);

    /* 挖空中心做成环 */
    GpSolidFill* hole = NULL;
    if (GdipCreateSolidFill(seam_argb, &hole) == Ok && hole) {
        GdipFillEllipse(gfx, hole, (REAL)(cx - ir), (REAL)(cy - ir),
                        (REAL)(ir * 2), (REAL)(ir * 2));
        GdipDeleteBrush(hole);
    }

    GdipDeleteGraphics(gfx);
}

static void pie_empty(HDC hdc, RECT* rc, const Theme* t) {
    Ui_Text(hdc, L"本月暂无数据", rc, t->text_dim, Ui_Font(UI_FONT_SMALL),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void pie_paint(HWND hw, HDC hdc0) {
    const Theme* t = Theme_Now();
    RECT rc;
    GetClientRect(hw, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;

    /* 双缓冲：GDI+ 抗锯齿 + 文字混排，直接画屏会闪 */
    HDC     mdc = CreateCompatibleDC(hdc0);
    HBITMAP bmp = CreateCompatibleBitmap(hdc0, W, H);
    HBITMAP ob  = (HBITMAP)SelectObject(mdc, bmp);

    Ui_FillSolid(mdc, &rc, t->surface);

    if (g_pie.n <= 0 || g_pie.total <= 0) {
        pie_empty(mdc, &rc, t);
        BitBlt(hdc0, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, ob);
        DeleteObject(bmp);
        DeleteDC(mdc);
        return;
    }

    /* 右侧留给图例（两列），剩下的方形区域画环 */
    int legend_w = Ui_S(150);   /* 单列图例，宽度让给环 */
    int box_w = rc.right - rc.left;
    int box_h = rc.bottom - rc.top;
    int area_w = box_w - legend_w;
    if (area_w < Ui_S(60)) area_w = box_w;

    int dia = (area_w < box_h ? area_w : box_h) - Ui_S(4);
    if (dia < Ui_S(40)) dia = Ui_S(40);
    int R  = dia / 2;
    int cx = rc.left + area_w / 2;
    int cy = rc.top + box_h / 2;
    int ir = R * 62 / 100;

    paint_donut(mdc, cx, cy, R, ir, t);

    /* --- 环心：总额 --- */
    {
        RECT r1 = { cx - ir + Ui_S(6), cy - Ui_S(21), cx + ir - Ui_S(6), cy - Ui_S(3) };
        RECT r2 = { cx - ir + Ui_S(6), cy - Ui_S(3),  cx + ir - Ui_S(6), cy + Ui_S(19) };
        WCHAR wmoney[64];
        char  mb[64];
        fmt_money_utf8(mb, sizeof(mb), g_pie.total, 2);
        utf8_to_wchar(mb, wmoney, 64);
        Ui_Text(mdc, L"本月支出", &r1, t->text_dim, Ui_Font(UI_FONT_SMALL),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        Ui_Text(mdc, wmoney, &r2, t->text, Ui_Font(UI_FONT_TITLE),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* --- 图例：单列竖排（最多 8 类），色点 + 分类 + 占比 --- */
    {
        int lx = rc.left + area_w + Ui_S(10);
        int rows = g_pie.n > 8 ? 8 : g_pie.n;
        if (rows < 1) rows = 1;
        int row_h = Ui_S(21);
        int col_w = Ui_S(140);
        int top = (rc.top + rc.bottom) / 2 - rows * row_h / 2;

        for (int i = 0; i < g_pie.n && i < 8; i++) {
            int x = lx;
            int y = top + i * row_h;

            RECT dr = { x, y + (row_h - Ui_S(9)) / 2,
                        x + Ui_S(9), y + (row_h - Ui_S(9)) / 2 + Ui_S(9) };
            Ui_FillRound(mdc, &dr, Ui_S(3), slice_color(g_pie.ci[i]));

            WCHAR wn[64];
            utf8_to_wchar(g_pie.name[i], wn, 64);
            RECT tr = { x + Ui_S(15), y, x + col_w - Ui_S(44), y + row_h };
            Ui_Text(mdc, wn, &tr, t->text_sub, Ui_Font(UI_FONT_SMALL),
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            char pb[32];
            snprintf(pb, sizeof(pb), "%.1f%%", g_pie.pct[i]);
            WCHAR wp[32];
            utf8_to_wchar(pb, wp, 32);
            RECT pr = { x + col_w - Ui_S(46), y, x + col_w, y + row_h };
            Ui_Text(mdc, wp, &pr, t->text, Ui_Font(UI_FONT_SMALL),
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    BitBlt(hdc0, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, ob);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

static LRESULT CALLBACK pie_proc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        pie_paint(hw, hdc);
        EndPaint(hw, &ps);
        return 0;
    }
    }
    (void)wp; (void)lp;
    return DefWindowProcW(hw, msg, wp, lp);
}

static int pie_register(HINSTANCE hInst) {
    if (g_pie_reg) return 1;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = pie_proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = PIE_CLASS;
    if (!RegisterClassW(&wc)) return 0;
    g_pie_reg = 1;
    return 1;
}

HWND Ui_PieCreate(HWND parent, int id, int x, int y, int w, int h) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(NULL);
    if (!pie_register(hInst)) return NULL;

    HWND hw = CreateWindowExW(0, PIE_CLASS, L"", WS_CHILD | WS_VISIBLE,
                              Ui_S(x), Ui_S(y), Ui_S(w), Ui_S(h),
                              parent, (HMENU)(INT_PTR)id, hInst, NULL);
    g_pie_hw = hw;
    return hw;
}
