#include "dlg.h"
#include "ui.h"
#include "app_ui.h"
#include "theme.h"
#include "calendar.h"
#include "util.h"
#include <commctrl.h>
#include <windowsx.h>
#include <stdlib.h>
#include <string.h>

void Ui_ScrollAttach(HWND hList);

#ifndef CS_DROPSHADOW
#define CS_DROPSHADOW 0x00020000
#endif

#define DLG_CLASS L"TodoDlg"

#define MSG_INFO  0
#define MSG_WARN  1
#define MSG_ERROR 2
#define MSG_YESNO 3

typedef struct {
    HWND      hwnd;
    HWND      owner;
    UiDlgProc proc;
    WCHAR     title[128];
    int       hover_close;
    int       press_close;
    int       done;
    int       result;
    int       front;        /* 1 = 后台提醒：自己抢前台并置顶，owner 不动 */
} DlgState;

static ATOM g_dlg_atom = 0;
static HBRUSH g_surf_br = NULL;
static HBRUSH g_input_br = NULL;

static HBRUSH surf_brush(void) {
    if (!g_surf_br) g_surf_br = CreateSolidBrush(Theme_Now()->surface);
    return g_surf_br;
}

static HBRUSH input_brush(void) {
    if (!g_input_br) g_input_br = CreateSolidBrush(Theme_Now()->input_bg);
    return g_input_br;
}

/* 主题可能在两次对话框之间切换，重建刷子 */
static void reset_brushes(void) {
    if (g_surf_br)  { DeleteObject(g_surf_br);  g_surf_br = NULL; }
    if (g_input_br) { DeleteObject(g_input_br); g_input_br = NULL; }
}

void Ui_DlgSkin(HWND hDlg);

/* 给输入类控件画圆角槽（子窗口自身不画边框，靠父窗口衬底） */
static BOOL CALLBACK slot_cb(HWND hc, LPARAM lp) {
    HDC hdc = (HDC)lp;
    WCHAR cls[64];
    GetClassNameW(hc, cls, 64);
    int is_edit = (_wcsicmp(cls, L"Edit") == 0);
    int is_list = (_wcsicmp(cls, L"SysListView32") == 0);
    if (!is_edit && !is_list) return TRUE;
    if (!IsWindowVisible(hc)) return TRUE;

    RECT r;
    GetWindowRect(hc, &r);
    POINT p = { r.left, r.top };
    ScreenToClient(GetParent(hc), &p);
    int pad = Ui_S(2);
    RECT slot = { p.x - pad, p.y - pad,
                  p.x + (r.right - r.left) + pad,
                  p.y + (r.bottom - r.top) + pad };

    /* 日期 / 月份框：槽往右多留一个图标位，图标由父窗口画 */
    int calpad = is_edit ? (int)(INT_PTR)GetPropW(hc, L"calpad") : 0;
    if (calpad) slot.right += Ui_S(UI_CAL_ICON_W);

    const Theme* t = Theme_Now();
    if (is_edit) {
        Ui_FillRound(hdc, &slot, Ui_S(6), t->input_bg);
        Ui_StrokeRound(hdc, &slot, Ui_S(6), t->input_border, 1.0f);
        if (calpad) {
            int s = Ui_S(13);
            int cx = slot.right - Ui_S(10) - s / 2;
            int cy = (slot.top + slot.bottom) / 2;
            if (calpad == 3) Ui_DrawClockGlyph(hdc, cx, cy, s, t->text_dim);
            else             Ui_DrawCalGlyph(hdc, cx, cy, s, t->text_dim);
        }
    } else {
        Ui_StrokeRound(hdc, &slot, Ui_S(4), t->border, 1.0f);
    }
    return TRUE;
}

static DlgState* st_of(HWND h) {
    return (DlgState*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

static int title_h(void) { return Ui_S(UI_DLG_TITLE_H); }

static RECT close_rect(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    RECT r;
    int w = Ui_S(40);
    int hh = Ui_S(26);
    r.right = rc.right - Ui_S(6);
    r.left = r.right - w;
    r.top = (title_h() - hh) / 2;
    r.bottom = r.top + hh;
    return r;
}

/* 点到了日期/月份框右侧的图标空白区？
 * 那块区域在 Edit 子窗口之外，消息进不了 date_sub，只能由父窗口代转。 */
static HWND cal_icon_hit(HWND d, POINT p) {
    HWND hc = GetWindow(d, GW_CHILD);
    while (hc) {
        if (GetPropW(hc, L"calpad")) {
            RECT r;
            if (GetWindowRect(hc, &r)) {
                POINT tl = { r.left, r.top };
                POINT br = { r.right, r.bottom };
                ScreenToClient(d, &tl);
                ScreenToClient(d, &br);
                RECT ic = { br.x - Ui_S(6), tl.y, br.x + Ui_S(UI_CAL_ICON_W), br.y };
                if (PtInRect(&ic, p)) return hc;
            }
        }
        hc = GetWindow(hc, GW_HWNDNEXT);
    }
    return NULL;
}

static void dlg_paint(HWND h, HDC hdc) {
    DlgState* s = st_of(h);
    const Theme* t = Theme_Now();
    RECT rc;
    GetClientRect(h, &rc);

    /* 调用方已完成背景 + 内容的双缓冲合成，这里只叠加标题栏、关闭按钮和外框 */
    {
        RECT tr = { 0, 0, rc.right, title_h() };
        Ui_FillSolid(hdc, &tr, t->surface);
        RECT ln = { 0, title_h() - 1, rc.right, title_h() };
        Ui_FillSolid(hdc, &ln, t->border_soft);

        RECT txt = { Ui_S(16), 0, rc.right - Ui_S(52), title_h() };
        Ui_Text(hdc, s ? s->title : L"", &txt, t->text, Ui_Font(UI_FONT_BODY),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    {
        RECT cr = close_rect(h);
        COLORREF fg = t->text_sub;
        if (s && (s->hover_close || s->press_close)) {
            Ui_FillRound(hdc, &cr, Ui_S(5), s->press_close ? t->danger : t->hover_bg);
            fg = s->press_close ? RGB(255, 255, 255) : t->text;
        }
        int cx = (cr.left + cr.right) / 2;
        int cy = (cr.top + cr.bottom) / 2;
        int q = Ui_S(4);
        HPEN pen = CreatePen(PS_SOLID, 1, fg);
        HPEN op = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, cx - q, cy - q, NULL);
        LineTo(hdc, cx + q + 1, cy + q + 1);
        MoveToEx(hdc, cx + q, cy - q, NULL);
        LineTo(hdc, cx - q - 1, cy + q + 1);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }

    /* 输入框 / 列表的圆角槽 */
    EnumChildWindows(h, slot_cb, (LPARAM)hdc);

    {
        RECT b = rc;
        Ui_StrokeRound(hdc, &b, Ui_S(8), t->border, 1.0f);
    }
}

static LRESULT CALLBACK DlgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    DlgState* s = st_of(h);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        s = (DlgState*)cs->lpCreateParams;
        s->hwnd = h;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);

            Ui_FillSolid(mem, &rc, Theme_Now()->surface);
            if (s && s->proc) s->proc(h, WM_APP + 1, (WPARAM)mem, 0);
            dlg_paint(h, mem);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (s) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT cr = close_rect(h);
            int hv = PtInRect(&cr, p) ? 1 : 0;
            if (hv != s->hover_close) {
                s->hover_close = hv;
                InvalidateRect(h, &cr, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (s && s->hover_close) {
            s->hover_close = 0;
            RECT cr = close_rect(h);
            InvalidateRect(h, &cr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (s) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT cr = close_rect(h);
            if (PtInRect(&cr, p)) {
                s->press_close = 1;
                SetCapture(h);
                InvalidateRect(h, &cr, FALSE);
                return 0;
            }
            {
                HWND cal = cal_icon_hit(h, p);
                if (cal) {
                    SendMessageW(cal, WM_LBUTTONDOWN, 0, 0);
                    return 0;
                }
            }
        }
        break;

    case WM_LBUTTONUP:
        if (s && s->press_close) {
            s->press_close = 0;
            ReleaseCapture();
            RECT cr = close_rect(h);
            InvalidateRect(h, &cr, FALSE);
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&cr, p)) Ui_DlgFinish(h, 0);
            return 0;
        }
        break;

    case WM_NCHITTEST: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &p);
        RECT cr = close_rect(h);
        if (PtInRect(&cr, p)) return HTCLIENT;
        if (p.y < title_h()) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_CLOSE:
        if (s) Ui_DlgFinish(h, 0);
        return 0;

    case WM_COMMAND:
        if (s && s->proc) {
            if (s->proc(h, msg, wp, lp)) return 0;
        }
        break;

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (dis && dis->CtlType == ODT_BUTTON) {
            /* 带 "prio" 属性的按钮（待办优先级三选一）交给主界面那套绘制，
               颜色按优先级走红 / 橙 / 灰，而不是统一的主色蓝 */
            if (GetPropW(dis->hwndItem, L"prio")) {
                Ui_DrawOwnerdrawButton(dis);
                return TRUE;
            }
            /* 复用主窗口按钮绘制，但按钮 ID 100+ 需要映射 kind */
            const Theme* t = Theme_Now();
            RECT rc = dis->rcItem;
            Ui_FillSolid(dis->hDC, &rc, t->surface);
            int state = UI_ST_NORMAL;
            if (dis->itemState & ODS_DISABLED) state = UI_ST_DISABLED;
            else if (dis->itemState & ODS_SELECTED) state = UI_ST_PRESSED;
            else if (GetPropW(dis->hwndItem, L"hovered")) state = UI_ST_HOVER;

            WCHAR text[128];
            GetWindowTextW(dis->hwndItem, text, 128);
            int kind = GetPropW(dis->hwndItem, L"primary") ? UI_BTN_PRIMARY : UI_BTN_SECONDARY;
            Ui_DrawButton(dis->hDC, &rc, text, kind, state);
            return TRUE;
        }
        if (dis && dis->CtlType == ODT_LISTBOX) {
            const Theme* t = Theme_Now();
            RECT rc = dis->rcItem;
            int sel = (dis->itemID != (UINT)-1) && (dis->itemState & ODS_SELECTED);
            Ui_FillSolid(dis->hDC, &rc, sel ? t->selection : t->list_bg);
            if (dis->itemID != (UINT)-1) {
                WCHAR text[512];
                int n = (int)SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
                if (n != LB_ERR) {
                    RECT tr = rc;
                    tr.left += Ui_S(10);
                    Ui_Text(dis->hDC, text, &tr, sel ? t->accent_soft_fg : t->text,
                            Ui_Font(UI_FONT_SMALL), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
            }
            if (dis->itemState & ODS_FOCUS) {
                RECT f = rc;
                f.left += Ui_S(8);
                f.right -= Ui_S(8);
            }
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh && nh->code == NM_CUSTOMDRAW) {
            /* 与主界面列表共用同一套自绘（含分类色块），保证两处配色一致 */
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT ||
                cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT ||
                cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
                return Ui_ListCustomDraw(cd);

            const Theme* t = Theme_Now();
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                HBRUSH br = CreateSolidBrush(t->list_bg);
                FillRect(cd->nmcd.hdc, &cd->nmcd.rc, br);
                DeleteObject(br);
                return CDRF_NOTIFYITEMDRAW;
            }
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int idx = (int)cd->nmcd.dwItemSpec;
                DWORD stt = (DWORD)SendMessageW(nh->hwndFrom, LVM_GETITEMSTATE, idx, LVIS_SELECTED);
                int sel = (stt & LVIS_SELECTED) != 0;
                COLORREF bg = sel ? t->selection : ((idx & 1) ? t->surface_alt : t->list_bg);
                /* 清掉选中标记，避免系统用默认高亮蓝覆盖自绘底色 */
                if (sel) cd->nmcd.uItemState &= ~CDIS_SELECTED;
                HBRUSH br = CreateSolidBrush(bg);
                FillRect(cd->nmcd.hdc, &cd->nmcd.rc, br);
                DeleteObject(br);
                cd->clrText = sel ? t->accent_soft_fg : t->text;
                cd->clrTextBk = bg;
                return CDRF_NEWFONT;
            }
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        const Theme* t = Theme_Now();
        SetTextColor(dc, t->text);
        SetBkColor(dc, t->input_bg);
        return (LRESULT)input_brush();
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        const Theme* t = Theme_Now();
        SetTextColor(dc, t->text_sub);
        SetBkColor(dc, t->surface);
        return (LRESULT)surf_brush();
    }

    case WM_ACTIVATE:
        if (s && LOWORD(wp) == WA_INACTIVE && s->press_close) {
            s->press_close = 0;
        }
        return 0;
    }

    return DefWindowProcW(h, msg, wp, lp);
}

static LRESULT CALLBACK btn_hover(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_MOUSEMOVE) {
        if (!GetPropW(h, L"hovered")) {
            SetPropW(h, L"hovered", (HANDLE)1);
            InvalidateRect(h, NULL, FALSE);
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
        }
    } else if (msg == WM_MOUSELEAVE) {
        if (GetPropW(h, L"hovered")) {
            RemovePropW(h, L"hovered");
            InvalidateRect(h, NULL, FALSE);
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

/* 日期框单击弹日历；ref=2 只选年月；ref=3 只选时间（禁止手打） */
static LRESULT CALLBACK date_sub(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)wp; (void)lp;
    if (msg == WM_LBUTTONDOWN) {
        WCHAR cur[64] = {0}, out[16] = {0};
        GetWindowTextW(h, cur, 64);
        HWND root = GetAncestor(h, GA_ROOT);
        int ok;
        if (ref == 3)      ok = Ui_PickTime(root, h, 0, 0, cur, out, 16);
        else if (ref == 2) ok = Ui_PickMonth(root, h, 0, 0, cur, out, 16);
        else               ok = Ui_PickDate(root, h, 0, 0, cur, out, 16);
        if (ok) {
            SetWindowTextW(h, out);
            SendMessageW(GetParent(h), WM_COMMAND,
                         MAKEWPARAM((WORD)GetDlgCtrlID(h), EN_CHANGE), (LPARAM)h);
        }
        return 0;
    }
    if (ref == 3) {
        /* 时间框只能从选择器里取，键盘输入一并吞掉，避免手打死值 */
        if (msg == WM_CHAR) return 0;
        if (msg == WM_PASTE || msg == WM_CUT || msg == WM_CLEAR) return 0;
        if (msg == WM_KEYDOWN && (wp == VK_BACK || wp == VK_DELETE)) return 0;
        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

/* 多行输入框：滚轮滚动（系统滚动条在深色主题下会突兀） */
static LRESULT CALLBACK edit_wheel(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR id, DWORD_PTR ref) {
    (void)lp; (void)id; (void)ref;
    if (msg == WM_MOUSEWHEEL) {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        SendMessageW(h, EM_LINESCROLL, 0, -delta / WHEEL_DELTA * 3);
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static int dlg_register(HINSTANCE hInst) {
    if (g_dlg_atom) return 1;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = DlgWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = DLG_CLASS;
    wc.style = CS_DROPSHADOW;
    g_dlg_atom = RegisterClassW(&wc);
    return g_dlg_atom != 0;
}

HWND Ui_DlgBegin(HWND owner, const WCHAR* title, int w, int h, UiDlgProc proc) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!hInst) hInst = GetModuleHandleW(NULL);
    if (!dlg_register(hInst)) return NULL;

    reset_brushes();
    DlgState* s = (DlgState*)calloc(1, sizeof(DlgState));
    if (!s) return NULL;
    s->owner = owner;
    s->proc = proc;
    wcsncpy(s->title, title ? title : L"", 127);

    int W = MulDiv(w, Ui_Dpi(), 96);
    int H = MulDiv(h, Ui_Dpi(), 96);

    /* 居中于 owner。owner 最小化 / 隐藏到托盘时按「恢复后的位置」算会弹到角落，
       所以这种情况下改成屏幕工作区居中 —— 看着才像一张通知，而不是某个隐藏窗口的子窗口。 */
    RECT orc = {0};
    int x = 0, y = 0;
    int owner_off = (!owner || !IsWindowVisible(owner) || IsIconic(owner));
    if (!owner_off && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - W) / 2;
        y = orc.top + ((orc.bottom - orc.top) - H) / 2;
    }
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (owner_off) {
        x = wa.left + ((wa.right - wa.left) - W) / 2;
        y = wa.top + ((wa.bottom - wa.top) - H) / 2;
    }
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + W > wa.right) x = wa.right - W;
    if (y + H > wa.bottom) y = wa.bottom - H;

    HWND hDlg = CreateWindowExW(0, DLG_CLASS, s->title,
                                WS_POPUP | WS_CLIPCHILDREN,
                                x, y, W, H, owner, NULL, hInst, s);
    if (!hDlg) { free(s); return NULL; }

    SendMessageW(hDlg, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadIconW(hInst, L"MAINICON"));
    return hDlg;
}

void Ui_DlgFinish(HWND hDlg, int result) {
    DlgState* s = st_of(hDlg);
    if (!s) return;
    s->result = result;
    s->done = 1;
    PostMessageW(hDlg, WM_NULL, 0, 0);
}

int Ui_DlgRun(HWND hDlg) {
    DlgState* s = st_of(hDlg);
    if (!s) return 0;

    EnableWindow(s->owner, FALSE);
    if (s->front) {
        /* 后台提醒（倒计时到点之类）：不碰主窗口，只把自己顶到最前。
           普通 ShowWindow(SW_SHOW) 是「不激活」的，提示框可能落在别的窗口后面
           —— 这就是「有时候根本看不到提醒」的根源。置顶 + 抢前台 + 闪边框三件套。 */
        SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hDlg);
        FLASHWINFO fw;
        memset(&fw, 0, sizeof(fw));
        fw.cbSize = sizeof(fw);
        fw.hwnd = hDlg;
        fw.dwFlags = FLASHW_ALL;
        fw.uCount = 3;
        fw.dwTimeout = 0;
        FlashWindowEx(&fw);
    } else {
        ShowWindow(hDlg, SW_SHOW);
    }
    SetFocus(hDlg);

    MSG msg;
    while (!s->done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN)) {
            HWND focus = GetFocus();
            WCHAR cls[64] = {0};
            if (focus) GetClassNameW(focus, cls, 64);
            int multiline = 0;
            if (focus && _wcsicmp(cls, L"Edit") == 0) {
                LONG st = GetWindowLongW(focus, GWL_STYLE);
                /* 逻辑单行的输入框由 Ui_EditVAlign 做成多行，回车仍应提交 */
                multiline = ((st & ES_MULTILINE) != 0) && !Ui_EditIsSingle(focus);
            }
            if (msg.wParam == VK_ESCAPE) {
                Ui_DlgFinish(hDlg, 0);
                continue;
            }
            if (!multiline) {
                HWND ok = GetDlgItem(hDlg, IDOK);
                if (ok) SendMessageW(ok, BM_CLICK, 0, 0);
                continue;
            }
        }
        if (IsDialogMessageW(hDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    int result = s->result;
    EnableWindow(s->owner, TRUE);
    /* front 模式（后台提醒）不把焦点还给主窗口：主窗口该最小化就继续最小化、该藏托盘就继续藏 */
    if (!s->front) SetForegroundWindow(s->owner);
    DestroyWindow(hDlg);
    free(s);
    return result;
}

/* ---------------- 控件工厂 ---------------- */

HWND Ui_DlgLabel(HWND d, const WCHAR* text, int x, int y, int w, int h) {
    HWND hc = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                              Ui_S(x), Ui_S(y), Ui_S(w), Ui_S(h),
                              d, NULL, (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE), NULL);
    SendMessageW(hc, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);
    return hc;
}

HWND Ui_DlgEdit(HWND d, int id, int x, int y, int w, int h, int multiline) {
    /*
     * 单行的情况也用 ES_MULTILINE 创建，再由 Ui_EditVAlign 把文本压成一条居中的
     * 横带：原生单行 EDIT 不支持 EM_SETRECT，字会永远贴在框顶。
     */
    DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
               (multiline ? ES_AUTOVSCROLL : ES_AUTOHSCROLL);
    HWND hc = CreateWindowExW(0, L"EDIT", L"", st,
                              Ui_S(x + 2), Ui_S(y + 2), Ui_S(w - 4), Ui_S(h - 4),
                              d, (HMENU)(INT_PTR)id,
                              (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE), NULL);
    SendMessageW(hc, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);
    SendMessageW(hc, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(Ui_S(8), Ui_S(8)));
    if (multiline) SetWindowSubclass(hc, edit_wheel, 3, 0);
    else           Ui_EditVAlign(hc);
    return hc;
}

HWND Ui_DlgEditCenter(HWND d, int id, int x, int y, int w, int h) {
    HWND hc = Ui_DlgEdit(d, id, x, y, w, h, 0);
    if (hc) {
        LONG st = GetWindowLongW(hc, GWL_STYLE);
        SetWindowLongW(hc, GWL_STYLE, st | ES_CENTER);
        Ui_EditVAlign(hc);   /* 重跑一次，把段落对齐刷成居中 */
    }
    return hc;
}

HWND Ui_DlgButton(HWND d, int id, const WCHAR* text, int x, int y, int w, int h, int primary) {
    HWND hc = CreateWindowW(L"BUTTON", text,
                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                            Ui_S(x), Ui_S(y), Ui_S(w), Ui_S(h),
                            d, (HMENU)(INT_PTR)id,
                            (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE), NULL);
    SendMessageW(hc, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);
    if (primary) SetPropW(hc, L"primary", (HANDLE)1);
    SetWindowSubclass(hc, btn_hover, 1, 0);
    return hc;
}

HWND Ui_DlgList(HWND d, int id, int x, int y, int w, int h) {
    HWND hc = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                              LVS_NOSORTHEADER | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | WS_TABSTOP,
                              Ui_S(x), Ui_S(y), Ui_S(w), Ui_S(h),
                              d, (HMENU)(INT_PTR)id,
                              (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE), NULL);
    ListView_SetExtendedListViewStyle(hc, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(hc, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);
    {
        /* 每个列表各自建一份行高图（ListView 销毁时会连带释放它，共用静态句柄会失效） */
        HIMAGELIST il = ImageList_Create(1, Ui_S(30), ILC_COLOR32, 1, 1);
        if (il) SendMessageW(hc, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)il);
    }
    Ui_ScrollAttach(hc);
    return hc;
}

static HWND date_like(HWND d, int id, int x, int y, int w, int h,
                      int kind, int center) {
    /* kind: 0 = 日期，1 = 月份，2 = 时间 */
    DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL;
    if (center) st |= ES_CENTER;
    HWND hc = CreateWindowExW(0, L"EDIT", L"", st,
                              Ui_S(x + 2), Ui_S(y + 2),
                              Ui_S(w - 4 - UI_CAL_ICON_W), Ui_S(h - 4),
                              d, (HMENU)(INT_PTR)id,
                              (HINSTANCE)GetWindowLongPtrW(d, GWLP_HINSTANCE), NULL);
    SendMessageW(hc, WM_SETFONT, (WPARAM)Ui_Font(UI_FONT_SMALL), TRUE);
    SendMessageW(hc, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(Ui_S(10), Ui_S(4)));
    Ui_EditVAlign(hc);
    Ui_EditSetHint(hc, kind == 2 ? L"HH:MM"
                      : (kind == 1 ? L"YYYY-MM" : L"YYYY-MM-DD"));
    /* calpad 让父窗口在槽右侧画出图标（1 日历 / 2 月份 / 3 时钟）并把槽撑宽 */
    SetPropW(hc, L"calpad", (HANDLE)(INT_PTR)(kind + 1));
    SetWindowSubclass(hc, date_sub, 2,
                      (DWORD_PTR)(kind == 0 ? 0 : (kind == 1 ? 2 : 3)));
    return hc;
}

HWND Ui_DlgDate(HWND d, int id, int x, int y, int w, int h) {
    return date_like(d, id, x, y, w, h, 0, 0);
}

HWND Ui_DlgDateCenter(HWND d, int id, int x, int y, int w, int h) {
    return date_like(d, id, x, y, w, h, 0, 1);
}

HWND Ui_DlgMonth(HWND d, int id, int x, int y, int w, int h) {
    return date_like(d, id, x, y, w, h, 1, 1);
}

HWND Ui_DlgTime(HWND d, int id, int x, int y, int w, int h) {
    return date_like(d, id, x, y, w, h, 2, 1);
}

void Ui_DlgSkin(HWND hDlg) {
    (void)hDlg;
}


/* ---------------- 通用消息框 ---------------- */

typedef struct {
    const WCHAR* text;
    int          kind;
} MsgInfo;

static int msg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    MsgInfo* mi = (MsgInfo*)GetPropW(h, L"msg");

    if (msg == WM_APP + 1) {
        HDC dc = (HDC)wp;
        const Theme* t = Theme_Now();
        int top = title_h() + Ui_S(22);

        /* 左侧圆形图标：用形状表达语气，比一个色块圆点清楚 */
        int kind = mi ? mi->kind : MSG_INFO;
        COLORREF c = (kind == MSG_ERROR) ? t->danger
                   : (kind == MSG_WARN)  ? RGB(0xE0, 0xA8, 0x4E)
                                         : t->accent;
        int d = Ui_S(24);
        int ix = Ui_S(22), iy = top - Ui_S(2);
        RECT ic = { ix, iy, ix + d, iy + d };
        Ui_FillRound(dc, &ic, d / 2, c);

        {
            int cx = (ic.left + ic.right) / 2;
            int cy = (ic.top + ic.bottom) / 2;
            int u = Ui_S(kind == MSG_INFO ? 5 : 1);
            HPEN pen = CreatePen(PS_SOLID, Ui_S(2) < 1 ? 1 : Ui_S(2), RGB(0xFF, 0xFF, 0xFF));
            HPEN op = (HPEN)SelectObject(dc, pen);
            if (kind == MSG_ERROR) {
                MoveToEx(dc, cx - u, cy - u, NULL);
                LineTo(dc, cx + u + 1, cy + u + 1);
                MoveToEx(dc, cx + u, cy - u, NULL);
                LineTo(dc, cx - u - 1, cy + u + 1);
            } else if (kind == MSG_WARN) {
                /* 感叹号：竖线 + 下方圆点（小尺寸下用实心方块，GDI 的 Ellipse 会画成空心环） */
                MoveToEx(dc, cx, cy - Ui_S(7), NULL);
                LineTo(dc, cx, cy + Ui_S(1));
                int dd = Ui_S(3);
                if (dd < 2) dd = 2;
                RECT dot = { cx - dd / 2, cy + Ui_S(4), cx - dd / 2 + dd, cy + Ui_S(4) + dd };
                HBRUSH wb = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
                FillRect(dc, &dot, wb);
                DeleteObject(wb);
            } else {
                /* 对勾 */
                int k = Ui_S(6);
                MoveToEx(dc, cx - k, cy, NULL);
                LineTo(dc, cx - k / 4, cy + k * 3 / 4);
                LineTo(dc, cx + k, cy - k * 3 / 4);
            }
            SelectObject(dc, op);
            DeleteObject(pen);
        }

        RECT tr;
        GetClientRect(h, &tr);
        tr.left = ix + d + Ui_S(14);
        tr.right -= Ui_S(26);
        tr.top = top - Ui_S(2);
        Ui_Text(dc, mi ? mi->text : L"", &tr, t->text, Ui_Font(UI_FONT_SMALL),
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        return 0;
    }

    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDOK) { Ui_DlgFinish(h, 1); return 1; }
        if (id == IDCANCEL) { Ui_DlgFinish(h, 0); return 1; }
    }
    return 0;
}

static int msg_box_impl(HWND owner, const WCHAR* title, const WCHAR* text, int kind, int front) {
    MsgInfo mi;
    mi.text = text;
    mi.kind = kind;

    /* 先量文字高度，决定窗口高度 */
    int text_w = 300;
    int th = Ui_S(40);
    {
        HDC dc = GetDC(owner);
        HFONT of = (HFONT)SelectObject(dc, Ui_Font(UI_FONT_SMALL));
        RECT tr = { 0, 0, MulDiv(text_w, Ui_Dpi(), 96), 0 };
        DrawTextW(dc, text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        th = tr.bottom;
        SelectObject(dc, of);
        ReleaseDC(owner, dc);
    }
    int th_log = MulDiv(th, 96, Ui_Dpi()) + 10;   /* 物理像素折回逻辑像素 */

    int w = 400;
    int h = UI_DLG_TITLE_H + 16 + (th_log < 30 ? 30 : th_log) + 14 + 44 + 12;
    if (h < 156) h = 156;

    HWND d = Ui_DlgBegin(owner, title, w, h, msg_proc);
    if (!d) return 0;
    if (front) {
        DlgState* st = st_of(d);
        if (st) st->front = 1;
    }
    SetPropW(d, L"msg", (HANDLE)&mi);

    int by = h - 44 - 12;
    if (kind == MSG_YESNO) {
        Ui_DlgButton(d, IDCANCEL, L"取消", w - 16 - 90 - 96, by, 90, 34, 0);
        Ui_DlgButton(d, IDOK, L"确定", w - 16 - 90, by, 90, 34, 1);
    } else {
        Ui_DlgButton(d, IDOK, L"知道了", w - 16 - 100, by, 100, 34, 1);
    }

    int r = Ui_DlgRun(d);
    return r;
}

int Ui_MsgBox(HWND owner, const WCHAR* title, const WCHAR* text, int kind) {
    return msg_box_impl(owner, title, text, kind, 0);
}

int Ui_MsgBoxFront(HWND owner, const WCHAR* title, const WCHAR* text, int kind) {
    return msg_box_impl(owner, title, text, kind, 1);
}
