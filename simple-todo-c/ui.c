#include "ui.h"
#include <gdiplus/gdiplus.h>
#include <commctrl.h>
#include <richedit.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

static ULONG_PTR g_token = 0;
static int       g_ready = 0;
static int       g_dpi = 96;
static HFONT     g_fonts[UI_FONT_COUNT];

int Ui_Dpi(void) { return g_dpi; }
int Ui_S(int v) { return MulDiv(v, g_dpi, 96); }

static ARGB to_argb(COLORREF c) {
    return (ARGB)0xFF000000 | ((ARGB)GetRValue(c) << 16) |
           ((ARGB)GetGValue(c) << 8) | (ARGB)GetBValue(c);
}

static void make_font(HFONT* out, int px, int weight, const WCHAR* face) {
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
    *out = CreateFontIndirectW(&lf);
}

void Ui_Init(void) {
    if (g_ready) return;

    {
        HDC screen = GetDC(NULL);
        if (screen) {
            g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
            ReleaseDC(NULL, screen);
        }
        if (g_dpi < 96) g_dpi = 96;
        if (g_dpi > 288) g_dpi = 288;
    }

    GdiplusStartupInput input;
    memset(&input, 0, sizeof(input));
    input.GdiplusVersion = 1;
    input.DebugEventCallback = NULL;
    input.SuppressBackgroundThread = FALSE;
    input.SuppressExternalCodecs = FALSE;
    if (GdiplusStartup(&g_token, &input, NULL) != Ok) {
        g_token = 0;
    }

    make_font(&g_fonts[UI_FONT_BODY],  Ui_S(13), FW_NORMAL,   L"Microsoft YaHei UI");
    make_font(&g_fonts[UI_FONT_SMALL], Ui_S(12), FW_NORMAL,   L"Microsoft YaHei UI");
    make_font(&g_fonts[UI_FONT_TITLE], Ui_S(15), FW_SEMIBOLD, L"Microsoft YaHei UI");
    make_font(&g_fonts[UI_FONT_BIG],   Ui_S(30), FW_SEMIBOLD, L"Microsoft YaHei UI");
    make_font(&g_fonts[UI_FONT_MONO],  Ui_S(15), FW_NORMAL,   L"Consolas");
    make_font(&g_fonts[UI_FONT_BRAND], Ui_S(13), FW_SEMIBOLD, L"Microsoft YaHei UI");
    make_font(&g_fonts[UI_FONT_LABEL], Ui_S(12), FW_SEMIBOLD, L"Microsoft YaHei UI");

    for (int i = 0; i < UI_FONT_COUNT; i++) {
        if (!g_fonts[i]) g_fonts[i] = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    g_ready = 1;
}

void Ui_Shutdown(void) {
    for (int i = 0; i < UI_FONT_COUNT; i++) {
        if (g_fonts[i]) { DeleteObject(g_fonts[i]); g_fonts[i] = NULL; }
    }
    if (g_token) { GdiplusShutdown(g_token); g_token = 0; }
    g_ready = 0;
}

HFONT Ui_Font(int level) {
    if (level < 0 || level >= UI_FONT_COUNT) level = UI_FONT_BODY;
    return g_fonts[level];
}

static GpPath* path_round(const RECT* rc, int r) {
    GpPath* p = NULL;
    if (GdipCreatePath(FillModeAlternate, &p) != Ok || !p) return NULL;

    int l = rc->left, t = rc->top, rt = rc->right, b = rc->bottom;
    int w = rt - l, h = b - t;
    int d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;

    if (d < 2) {
        GdipAddPathLineI(p, l, t, rt, t);
        GdipAddPathLineI(p, rt, t, rt, b);
        GdipAddPathLineI(p, rt, b, l, b);
        GdipAddPathLineI(p, l, b, l, t);
        GdipClosePathFigure(p);
    } else {
        GdipAddPathArcI(p, l, t, d, d, 180.0f, 90.0f);
        GdipAddPathArcI(p, rt - d, t, d, d, 270.0f, 90.0f);
        GdipAddPathArcI(p, rt - d, b - d, d, d, 0.0f, 90.0f);
        GdipAddPathArcI(p, l, b - d, d, d, 90.0f, 90.0f);
        GdipClosePathFigure(p);
    }
    return p;
}

void Ui_FillSolid(HDC hdc, const RECT* rc, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, rc, br);
    DeleteObject(br);
}

void Ui_FillRound(HDC hdc, const RECT* rc, int radius, COLORREF c) {
    if (radius <= 0) { Ui_FillSolid(hdc, rc, c); return; }

    GpGraphics* g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != Ok || !g) { Ui_FillSolid(hdc, rc, c); return; }
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    GpPath* p = path_round(rc, radius);
    if (p) {
        GpSolidFill* br = NULL;
        if (GdipCreateSolidFill(to_argb(c), &br) == Ok && br) {
            GdipFillPath(g, (GpBrush*)br, p);
            GdipDeleteBrush((GpBrush*)br);
        }
        GdipDeletePath(p);
    }
    GdipDeleteGraphics(g);
}

void Ui_StrokeRound(HDC hdc, const RECT* rc, int radius, COLORREF c, float width) {
    /*
     * 1px 描边画在矩形边上时有一半落在矩形外侧被裁掉，右下两条边会变成半透明甚至消失。
     * 统一向内收半个线宽，保证整圈边框都完整可见。
     */
    RECT r = *rc;
    int in = (int)(width * 0.5f + 0.5f);
    if (in < 1) in = 1;
    if (r.right - r.left > in * 2 + 2 && r.bottom - r.top > in * 2 + 2) {
        r.left += in;
        r.top += in;
        r.right -= in;
        r.bottom -= in;
        if (radius > in) radius -= in;
    }

    GpGraphics* g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != Ok || !g) return;
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    GpPath* p = path_round(&r, radius);
    if (p) {
        GpPen* pen = NULL;
        if (GdipCreatePen1(to_argb(c), width, UnitPixel, &pen) == Ok && pen) {
            GdipDrawPath(g, pen, p);
            GdipDeletePen(pen);
        }
        GdipDeletePath(p);
    }
    GdipDeleteGraphics(g);
}

void Ui_Card(HDC hdc, const RECT* rc, int radius) {
    const Theme* t = Theme_Now();
    Ui_FillRound(hdc, rc, radius, t->surface);
    Ui_StrokeRound(hdc, rc, radius, t->border, 1.0f);
}

void Ui_GradientRound(HDC hdc, const RECT* rc, int radius, COLORREF a, COLORREF b, int vertical) {
    if (a == b) { Ui_FillRound(hdc, rc, radius, a); return; }

    GpGraphics* g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != Ok || !g) { Ui_FillRound(hdc, rc, radius, a); return; }
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    GpPath* p = path_round(rc, radius);
    if (p) {
        GpLineGradient* br = NULL;
        GpPoint pt1, pt2;
        pt1.X = rc->left; pt1.Y = rc->top;
        pt2.X = vertical ? rc->left : rc->right;
        pt2.Y = vertical ? rc->bottom : rc->top;
        if (GdipCreateLineBrushI(&pt1, &pt2, to_argb(a), to_argb(b),
                                 WrapModeTileFlipXY, &br) == Ok && br) {
            GdipFillPath(g, (GpBrush*)br, p);
            GdipDeleteBrush((GpBrush*)br);
        }
        GdipDeletePath(p);
    }
    GdipDeleteGraphics(g);
}

void Ui_Text(HDC hdc, const WCHAR* s, RECT* rc, COLORREF c, HFONT f, UINT flags) {
    if (!s) return;
    HFONT old = (HFONT)SelectObject(hdc, f ? f : Ui_Font(UI_FONT_BODY));
    int oldMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(hdc, c);
    DrawTextW(hdc, s, -1, rc, flags);
    SetTextColor(hdc, oldColor);
    SetBkMode(hdc, oldMode);
    SelectObject(hdc, old);
}

int Ui_TextWidth(HDC hdc, const WCHAR* s, HFONT f) {
    if (!s || !s[0]) return 0;
    HFONT old = (HFONT)SelectObject(hdc, f ? f : Ui_Font(UI_FONT_BODY));
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, s, (int)wcslen(s), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

void Ui_DrawButton(HDC hdc, const RECT* rc, const WCHAR* text, int kind, int state) {
    const Theme* t = Theme_Now();
    COLORREF fill, fg, bd;

    if (kind == UI_BTN_PRIMARY) {
        fill = (state == UI_ST_PRESSED) ? t->accent_down
             : (state == UI_ST_HOVER)   ? t->accent_hover
                                        : t->accent;
        fg = t->accent_fg;
        bd = fill;
    } else if (kind == UI_BTN_GHOST) {
        fill = (state == UI_ST_HOVER) ? t->accent_soft : t->surface;
        fg = (state == UI_ST_HOVER) ? t->accent_soft_fg : t->text_sub;
        bd = (state == UI_ST_HOVER) ? t->accent_soft : t->border_soft;
    } else {
        fill = (state == UI_ST_PRESSED) ? t->surface_alt : t->surface;
        fg = (state == UI_ST_DISABLED) ? t->text_dim : t->text;
        bd = t->border;
    }

    Ui_FillRound(hdc, rc, 6, fill);
    Ui_StrokeRound(hdc, rc, 6, bd, 1.0f);

    RECT tr = *rc;
    Ui_Text(hdc, text, &tr, fg, Ui_Font(UI_FONT_SMALL),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

/* kind: 0 = 太阳(切到浅色), 1 = 月牙(切到深色) */
void Ui_DrawIcon(HDC hdc, const RECT* rc, int kind, COLORREF c) {
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int r  = Ui_S(6);
    int r2 = Ui_S(4);

    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    if (kind == 0) {
        Ellipse(hdc, cx - r2, cy - r2, cx + r2 + 1, cy + r2 + 1);
        static const int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        static const int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
        for (int i = 0; i < 8; i++) {
            int x1 = cx + dx[i] * Ui_S(7), y1 = cy + dy[i] * Ui_S(7);
            int x2 = cx + dx[i] * Ui_S(9), y2 = cy + dy[i] * Ui_S(9);
            MoveToEx(hdc, x1, y1, NULL);
            LineTo(hdc, x2, y2);
        }
    } else {
        Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);
        HBRUSH bgBr = CreateSolidBrush(Theme_Now()->surface);
        SelectObject(hdc, bgBr);
        SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, cx - r + Ui_S(5), cy - r - Ui_S(2),
                cx + r + Ui_S(5), cy + r - Ui_S(2));
        DeleteObject(bgBr);
    }

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

/* ---------------- 列表完成标记 ---------------- */

/*
 * 原来用 "✓" / "○" 两个字符拼，字号跟着列表字体走，又小又难看，
 * 而且字符的视觉重心不在格子里。改成用 GDI+ 直接画：
 * 未完成 = 空心圆，已完成 = 绿色对勾。
 */
void Ui_DrawCheckMark(HDC hdc, int cx, int cy, int size, int done,
                      COLORREF done_color, COLORREF ring_color) {
    if (size < 8) size = 8;

    GpGraphics* g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != Ok || !g) return;
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    if (!done) {
        float pw = (size >= 14) ? 1.7f : 1.4f;
        float r  = (size - pw) / 2.0f;
        if (r < 1.0f) r = 1.0f;
        GpPen* pen = NULL;
        if (GdipCreatePen1(to_argb(ring_color), pw, UnitPixel, &pen) == Ok && pen) {
            GdipDrawEllipseI(g, pen, (int)(cx - r + 0.5f), (int)(cy - r + 0.5f),
                             (int)(r * 2), (int)(r * 2));
            GdipDeletePen(pen);
        }
    } else {
        float pw = (size >= 14) ? 2.4f : 2.0f;
        float k  = size / 2.0f;
        GpPen* pen = NULL;
        if (GdipCreatePen1(to_argb(done_color), pw, UnitPixel, &pen) == Ok && pen) {
            GdipSetPenStartCap(pen, LineCapRound);
            GdipSetPenEndCap(pen, LineCapRound);
            GdipSetPenLineJoin(pen, LineJoinRound);
            GpPointF pt[3];
            pt[0].X = (float)cx - k * 0.74f; pt[0].Y = (float)cy + k * 0.04f;
            pt[1].X = (float)cx - k * 0.16f; pt[1].Y = (float)cy + k * 0.58f;
            pt[2].X = (float)cx + k * 0.80f; pt[2].Y = (float)cy - k * 0.56f;
            GdipDrawLines(g, pen, pt, 3);
            GdipDeletePen(pen);
        }
    }
    GdipDeleteGraphics(g);
}

/* ---------------- 单行输入框：垂直居中 + 占位提示 ---------------- */

/*
 * 原生单行 EDIT 把文字贴着客户区顶部排版，控件比字高的时候字就"浮在上面"。
 * 单行 EDIT 不认 EM_SETRECT / EM_SETRECTNP，只有多行 EDIT 支持指定文本绘制矩形。
 * 所以这里统一把单行输入做成 ES_MULTILINE + 一条居中的文本横带，代价是两个
 * 单行专有行为要自己补回来：Enter 不再换行、EM_SETCUEBANNER 失效。
 */
#define VC_PROP L"vcEdit"
#define VC_HINT L"vcHint"

static void edit_vcenter_apply(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    int ch = rc.bottom - rc.top;
    if (ch <= 0) return;

    int lh = 0;
    HDC dc = GetDC(h);
    if (dc) {
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        HGDIOBJ of = SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW tm;
        if (GetTextMetricsW(dc, &tm)) lh = tm.tmHeight;
        SelectObject(dc, of);
        ReleaseDC(h, dc);
    }
    if (lh <= 0) lh = Ui_S(16);

    int top = (ch - lh) / 2;
    if (top < 0) top = 0;
    RECT r = { 0, top, rc.right, rc.bottom };
    SendMessageW(h, EM_SETRECTNP, 0, (LPARAM)&r);

    /*
     * ES_CENTER 对多行 EDIT 只作用于「垂直」方向，水平居中要另设段落格式。
     * 倒计时那几个窄框靠它才能把数字摆正中间。
     */
    LONG st = GetWindowLongW(h, GWL_STYLE);
    if ((st & ES_CENTER) && (st & ES_MULTILINE)) {
        PARAFORMAT2 pf;
        memset(&pf, 0, sizeof(pf));
        pf.cbSize = sizeof(pf);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageW(h, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
}

static void edit_draw_hint(HWND h) {
    const WCHAR* hint = (const WCHAR*)GetPropW(h, VC_HINT);
    if (!hint || !hint[0]) return;

    WCHAR first[4] = {0};
    SendMessageW(h, WM_GETTEXT, 4, (LPARAM)first);
    if (first[0]) return;

    HDC dc = GetDC(h);
    if (!dc) return;
    RECT rc;
    GetClientRect(h, &rc);
    rc.left += Ui_S(9);
    rc.right -= Ui_S(9);

    LONG st = GetWindowLongW(h, GWL_STYLE);
    UINT flags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    flags |= (st & ES_CENTER) ? DT_CENTER : DT_LEFT;

    HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    Ui_Text(dc, hint, &rc, Theme_Now()->text_dim,
            f ? f : Ui_Font(UI_FONT_SMALL), flags);
    ReleaseDC(h, dc);
}

static LRESULT CALLBACK edit_vc_sub(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_SIZE:
    case WM_SETFONT: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        edit_vcenter_apply(h);
        return r;
    }
    case WM_PAINT: {
        /*
         * 占位提示必须画在 Edit 自己的客户区里：父窗口画的任何东西都会被这个
         * 子窗口盖住，EM_SETCUEBANNER 又只支持真正的单行控件。
         */
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        edit_draw_hint(h);
        return r;
    }
    case WM_GETDLGCODE: {
        /* 逻辑单行的「伪装多行」框必须明确放弃 Tab：
           多行 EDIT 默认把 Tab 当自己的按键（DLGC_WANTALLKEYS），
           对话框管理器的 IsDialogMessage 拿不到，Tab 就死在框里 —— 表现为
           「按 Tab 不跳焦点」，随后 WM_CHAR 又被下面的 case 吞掉，连制表符都不进文本。
           真多行框（内容备注）不走这个 subclass，不受影响。 */
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        return r & ~(DLGC_WANTTAB | DLGC_WANTALLKEYS);
    }
    case WM_CHAR:
        /* 单行语义：回车/换行/制表符都不该落进文本 */
        if (wp == VK_RETURN || wp == VK_TAB || wp == L'\n' || wp == L'\r') return 0;
        break;
    case WM_PASTE: {
        if (!OpenClipboard(h)) break;
        HANDLE hd = GetClipboardData(CF_UNICODETEXT);
        const WCHAR* src = hd ? (const WCHAR*)GlobalLock(hd) : NULL;
        if (!src) { CloseClipboard(); break; }
        int n = (int)wcslen(src);
        WCHAR* buf = (WCHAR*)malloc((size_t)(n + 1) * sizeof(WCHAR));
        if (buf) {
            int k = 0;
            for (int i = 0; i < n; i++) {
                WCHAR c = src[i];
                buf[k++] = (c == L'\r' || c == L'\n' || c == L'\t') ? L' ' : c;
            }
            buf[k] = 0;
            GlobalUnlock(hd);
            CloseClipboard();
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)buf);
            free(buf);
            return 0;
        }
        GlobalUnlock(hd);
        CloseClipboard();
        break;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void Ui_EditVAlign(HWND hEdit) {
    if (!hEdit) return;
    if (!GetPropW(hEdit, VC_PROP)) {
        LONG st = GetWindowLongW(hEdit, GWL_STYLE);
        if (!(st & ES_MULTILINE)) return;
        SetPropW(hEdit, VC_PROP, (HANDLE)1);
        SetWindowSubclass(hEdit, edit_vc_sub, 11, 0);
    }
    edit_vcenter_apply(hEdit);
}

int Ui_EditIsSingle(HWND hEdit) {
    return hEdit && GetPropW(hEdit, VC_PROP) ? 1 : 0;
}

void Ui_EditSetHint(HWND hEdit, const WCHAR* hint) {
    if (!hEdit) return;
    SetPropW(hEdit, VC_HINT, (HANDLE)hint);
    InvalidateRect(hEdit, NULL, TRUE);
}

/* 分类色板：light bg / light fg / dark bg / dark fg */
static const COLORREF g_cat_pal[UI_CAT_PAL_N][4] = {
    /* 浅色主题：100 档明快底 + 700/800 档文字；深色主题：900 档底 + 300 档文字。
     * 16 个色相按色环铺开、明度对齐，同屏出现不会有的深有的浅、也不会撞色。 */
    { RGB(0xDB,0xEA,0xFE), RGB(0x1D,0x4E,0xD8), RGB(0x1E,0x3A,0x8A), RGB(0x93,0xC5,0xFD) }, /* 0 蓝 */
    { RGB(0xE0,0xF2,0xFE), RGB(0x03,0x69,0xA1), RGB(0x0C,0x4A,0x6E), RGB(0x7D,0xD3,0xFC) }, /* 1 天蓝 */
    { RGB(0xCF,0xFA,0xFE), RGB(0x0E,0x74,0x90), RGB(0x16,0x4E,0x63), RGB(0x67,0xE8,0xF9) }, /* 2 青 */
    { RGB(0xCC,0xFB,0xF1), RGB(0x0F,0x76,0x6E), RGB(0x13,0x4E,0x4A), RGB(0x5E,0xEA,0xD4) }, /* 3 青绿 */
    { RGB(0xDC,0xFC,0xE7), RGB(0x15,0x80,0x3D), RGB(0x14,0x53,0x2D), RGB(0x86,0xEF,0xAC) }, /* 4 绿 */
    { RGB(0xEC,0xFC,0xCB), RGB(0x4D,0x7C,0x0F), RGB(0x36,0x53,0x14), RGB(0xBE,0xF2,0x64) }, /* 5 橄榄 */
    { RGB(0xFE,0xF3,0xC7), RGB(0x92,0x40,0x0E), RGB(0x78,0x35,0x0F), RGB(0xFC,0xD3,0x4D) }, /* 6 琥珀 */
    { RGB(0xFF,0xED,0xD5), RGB(0xC2,0x41,0x0C), RGB(0x7C,0x2D,0x12), RGB(0xFD,0xBA,0x74) }, /* 7 橙 */
    { RGB(0xFE,0xE2,0xE2), RGB(0xB9,0x1C,0x1C), RGB(0x7F,0x1D,0x1D), RGB(0xFC,0xA5,0xA5) }, /* 8 红 */
    { RGB(0xFF,0xE4,0xE6), RGB(0xBE,0x12,0x3C), RGB(0x88,0x13,0x37), RGB(0xFD,0xA4,0xAF) }, /* 9 玫 */
    { RGB(0xFC,0xE7,0xF3), RGB(0xBE,0x18,0x5D), RGB(0x83,0x18,0x43), RGB(0xF9,0xA8,0xD4) }, /* 10 粉 */
    { RGB(0xF3,0xE8,0xFF), RGB(0x7E,0x22,0xCE), RGB(0x58,0x1C,0x87), RGB(0xD8,0xB4,0xFE) }, /* 11 紫 */
    { RGB(0xED,0xE9,0xFE), RGB(0x6D,0x28,0xD9), RGB(0x4C,0x1D,0x95), RGB(0xC4,0xB5,0xFD) }, /* 12 紫罗兰 */
    { RGB(0xE0,0xE7,0xFF), RGB(0x43,0x38,0xCA), RGB(0x31,0x2E,0x81), RGB(0xA5,0xB4,0xFC) }, /* 13 靛 */
    { RGB(0xE2,0xE8,0xF0), RGB(0x33,0x41,0x55), RGB(0x1E,0x29,0x3B), RGB(0xCB,0xD5,0xE1) }, /* 14 石板 */
    { RGB(0xF3,0xE8,0xDC), RGB(0x8A,0x53,0x30), RGB(0x3D,0x2A,0x1C), RGB(0xD3,0xAE,0x87) }, /* 15 棕 */
};

/* 图表色板：16 色相按色环铺开，统一 500 档明度（L≈55-60%），
 * 深浅主题共用同一套 —— 图表色本来就该是中间明度，跟着主题变反而会失去可比性。 */
static const COLORREF g_cat_chart[UI_CAT_PAL_N] = {
    RGB(0x3B,0x82,0xF6), /* 0  蓝 */
    RGB(0x0E,0xA5,0xE9), /* 1  天蓝 */
    RGB(0x06,0xB6,0xD4), /* 2  青 */
    RGB(0x14,0xB8,0xA6), /* 3  青绿 */
    RGB(0x22,0xC5,0x5E), /* 4  绿 */
    RGB(0x84,0xCC,0x16), /* 5  橄榄 */
    RGB(0xF5,0x9E,0x0B), /* 6  琥珀 */
    RGB(0xF9,0x73,0x16), /* 7  橙 */
    RGB(0xEF,0x44,0x44), /* 8  红 */
    RGB(0xF4,0x3F,0x5E), /* 9  玫 */
    RGB(0xEC,0x48,0x99), /* 10 粉 */
    RGB(0xA8,0x55,0xF7), /* 11 紫 */
    RGB(0x8B,0x5C,0xF6), /* 12 紫罗兰 */
    RGB(0x63,0x66,0xF1), /* 13 靛 */
    RGB(0x64,0x74,0x8B), /* 14 石板 */
    RGB(0x78,0x71,0x6C), /* 15 暖灰 */
};

void Ui_CatChartColor(int idx, COLORREF* c) {
    if (idx < 0) idx = -idx;
    idx %= UI_CAT_PAL_N;
    if (!c) return;

    COLORREF v = g_cat_chart[idx];
    /* 图表色板是 500 档高饱和色，铺在深色画布上会跳（红/紫/蓝尤其刺眼）。
       深色主题往白里混 26%：明度提一档、饱和降一档，环形图才不闹 */
    if (Theme_IsDark()) {
        int r = GetRValue(v), g = GetGValue(v), b = GetBValue(v);
        v = RGB(r + (255 - r) * 26 / 100,
                g + (255 - g) * 26 / 100,
                b + (255 - b) * 26 / 100);
    }
    *c = v;
}

int Ui_CatPresetIndex(const char* name) {
    /* 与 app_ui.c 的 kCategories 一一对应：色号固定，换月份也不会变色 */
    static const struct { const char* nm; int idx; } tab[] = {
        { "\xE9\xA4\x90\xE9\xA5\xAE",  7 },  /* 餐饮 -> 橙 */
        { "\xE8\xB4\xAD\xE7\x89\xA9",  0 },  /* 购物 -> 蓝 */
        { "\xE4\xBA\xA4\xE9\x80\x9A",  1 },  /* 交通 -> 天蓝 */
        { "\xE7\x94\x9F\xE6\xB4\xBB",  4 },  /* 生活 -> 绿 */
        { "\xE5\xA8\xB1\xE4\xB9\x90", 11 },  /* 娱乐 -> 紫 */
        { "\xE4\xBA\xBA\xE6\x83\x85", 10 },  /* 人情 -> 粉 */
        { "\xE5\x8C\xBB\xE7\x96\x97",  8 },  /* 医疗 -> 红 */
        { "\xE5\x85\xB6\xE4\xBB\x96", 14 },  /* 其他 -> 石板 */
    };
    if (!name || !name[0]) return -1;
    for (int i = 0; i < (int)(sizeof(tab) / sizeof(tab[0])); i++)
        if (strcmp(tab[i].nm, name) == 0) return tab[i].idx;
    return -1;
}

void Ui_CatChipColors(int idx, COLORREF* bg, COLORREF* fg) {
    if (idx < 0) idx = -idx;
    idx %= UI_CAT_PAL_N;
    int k = Theme_IsDark() ? 2 : 0;
    if (bg) *bg = g_cat_pal[idx][k];
    if (fg) *fg = g_cat_pal[idx][k + 1];
}

void Ui_DrawClockGlyph(HDC hdc, int cx, int cy, int size, COLORREF c) {
    if (size < 8) size = 8;
    int s = size;
    int r = s / 2;

    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HPEN op  = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

    /* 指针：时针向上，分针向右 */
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx, cy - r * 2 / 3);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + r * 2 / 3, cy);

    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pen);
}

void Ui_DrawCalGlyph(HDC hdc, int cx, int cy, int size, COLORREF c) {
    if (size < 8) size = 8;
    int s = size;
    RECT r = { cx - s / 2, cy - s / 2 + 1, cx + s / 2, cy + s / 2 + 1 };

    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HPEN op  = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    /* 外框 */
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);

    /* 表头横线（把日期格和挂钩分开） */
    int ly = r.top + (r.bottom - r.top) / 3;
    MoveToEx(hdc, r.left, ly, NULL);
    LineTo(hdc, r.right, ly);

    /* 两个挂钩 */
    int d = (r.right - r.left) / 4;
    MoveToEx(hdc, r.left + d, r.top - Ui_S(2), NULL);
    LineTo(hdc, r.left + d, r.top + 1);
    MoveToEx(hdc, r.right - d, r.top - Ui_S(2), NULL);
    LineTo(hdc, r.right - d, r.top + 1);

    /* 底部两个小点，暗示日期格 */
    int dot = Ui_S(1);
    if (dot < 1) dot = 1;
    int gy = ly + (r.bottom - ly) / 2;
    HBRUSH fb = CreateSolidBrush(c);
    HBRUSH ofb = (HBRUSH)SelectObject(hdc, fb);
    int gx1 = r.left + (r.right - r.left) / 3;
    int gx2 = r.left + (r.right - r.left) * 2 / 3;
    Rectangle(hdc, gx1, gy, gx1 + dot * 2, gy + dot * 2);
    Rectangle(hdc, gx2, gy, gx2 + dot * 2, gy + dot * 2);
    SelectObject(hdc, ofb);
    DeleteObject(fb);

    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(pen);
}
