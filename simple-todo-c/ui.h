#ifndef UI_H
#define UI_H

#include <windows.h>
#include "theme.h"

#define UI_FONT_BODY  0
#define UI_FONT_SMALL 1
#define UI_FONT_TITLE 2
#define UI_FONT_BIG   3
#define UI_FONT_MONO  4
#define UI_FONT_BRAND 5
#define UI_FONT_LABEL 6      /* 小号加粗：标签页 / 强调小字 */
#define UI_FONT_COUNT 7

#define UI_BTN_SECONDARY 0
#define UI_BTN_PRIMARY   1
#define UI_BTN_GHOST     2

#define UI_ST_NORMAL   0
#define UI_ST_HOVER    1
#define UI_ST_PRESSED  2
#define UI_ST_DISABLED 3

void  Ui_Init(void);
void  Ui_Shutdown(void);
HFONT Ui_Font(int level);
int   Ui_Dpi(void);
int   Ui_S(int v);

void  Ui_FillSolid(HDC hdc, const RECT* rc, COLORREF c);
void  Ui_FillRound(HDC hdc, const RECT* rc, int radius, COLORREF c);
void  Ui_StrokeRound(HDC hdc, const RECT* rc, int radius, COLORREF c, float width);
void  Ui_Card(HDC hdc, const RECT* rc, int radius);
void  Ui_GradientRound(HDC hdc, const RECT* rc, int radius, COLORREF a, COLORREF b, int vertical);

void  Ui_Text(HDC hdc, const WCHAR* s, RECT* rc, COLORREF c, HFONT f, UINT flags);
int   Ui_TextWidth(HDC hdc, const WCHAR* s, HFONT f);
void  Ui_DrawButton(HDC hdc, const RECT* rc, const WCHAR* text, int kind, int state);
void  Ui_DrawIcon(HDC hdc, const RECT* rc, int kind, COLORREF c);

/* 列表首列的完成标记：未完成 = 空心圆，已完成 = 绿色对勾 */
void  Ui_DrawCheckMark(HDC hdc, int cx, int cy, int size, int done,
                       COLORREF done_color, COLORREF ring_color);

/* 日期/月份框右侧留给日历图标的宽度 */
#define UI_CAL_ICON_W 24

/* 小日历图标（外框 + 顶部横线 + 两个挂钩），(cx, cy) 为中心点 */
void  Ui_DrawCalGlyph(HDC hdc, int cx, int cy, int size, COLORREF c);

/* 小钟表图标（圆 + 时针分针），(cx, cy) 为中心点 */
void  Ui_DrawClockGlyph(HDC hdc, int cx, int cy, int size, COLORREF c);

/* 分类色板：idx 取模后取色，自动跟随当前主题（浅/深） */
#define UI_CAT_PAL_N 16
void  Ui_CatChipColors(int idx, COLORREF* bg, COLORREF* fg);

/* 常见分类的固定色号（让「餐饮」永远是橙、「购物」永远是蓝）；
 * 未收录的分类返回 -1，交给调用方按名称哈希分配。 */
int   Ui_CatPresetIndex(const char* name);

/* 图表填充色：与 chip 同一色相、统一 500 档明度，专供环形图扇区使用
 * （chip 要浅底配深字，扇区要饱和度够高才好看，两者不能共用一套）。 */
void  Ui_CatChartColor(int idx, COLORREF* c);

/* 单行输入框：垂直居中 + 自绘占位提示（原生单行 EDIT 的字会贴在框顶） */
void  Ui_EditVAlign(HWND hEdit);
int   Ui_EditIsSingle(HWND hEdit);
void  Ui_EditSetHint(HWND hEdit, const WCHAR* hint);

#endif
