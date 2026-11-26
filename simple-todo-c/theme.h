#ifndef THEME_H
#define THEME_H

#include <windows.h>

typedef struct {
    int      dark;

    COLORREF bg;
    COLORREF surface;
    COLORREF surface_alt;
    COLORREF border;
    COLORREF border_soft;

    COLORREF text;
    COLORREF text_sub;
    COLORREF text_dim;

    COLORREF accent;
    COLORREF accent_hover;
    COLORREF accent_down;
    COLORREF accent_fg;
    COLORREF accent_soft;
    COLORREF accent_soft_fg;

    COLORREF selection;
    COLORREF sum_a;
    COLORREF sum_b;
    COLORREF sum_fg;
    COLORREF sum_sub;

    COLORREF input_bg;
    COLORREF input_border;   /* 输入框描边，比 border 深一档 */
    COLORREF list_bg;
    COLORREF hover_bg;
    COLORREF danger;
    COLORREF success;
    COLORREF warning;        /* 待办优先级「重要」用色 */
} Theme;

/* 必须在主窗口创建控件之前调用；config_path 可为 NULL（关闭持久化） */
void         Theme_Init(const WCHAR* config_path);
const Theme* Theme_Now(void);
int          Theme_IsDark(void);
void         Theme_SetDark(int dark);
void         Theme_Toggle(void);
void         Theme_Save(void);

#endif
