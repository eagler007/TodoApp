#ifndef APP_UI_H
#define APP_UI_H

#include "app.h"

#define UI_TOPBAR_H   48
#define UI_TIMERBAR_H 36
#define UI_TIMER_IN   24
#define UI_BOTTOM_GAP 10
#define UI_PAD        14
#define UI_INPUT_INSET_X 10
/* 圆角槽比输入框四周各大这么多。纵向必须是 2：1px 描边画在 slot.top+1 上，
   Edit 若从 slot.top+1 开始会正好把上边框盖掉（下边框同理）。 */
#define UI_INPUT_INSET_Y 2
#define UI_INPUT_H    30
#define UI_BTN_H      28
#define UI_LABEL_W    46
#define UI_DATE_CAL_PAD UI_CAL_ICON_W   /* 日期框右侧日历图标的宽度 */
#define UI_SUM_H      90     /* 记账页顶部「本月支出」汇总卡高度 */

/*
 * 存储层按 UTF-8 字节计容量，EDIT 的 EM_SETLIMITTEXT 按字符计。
 * 中文 1 字占 3 字节，所以字符上限取 (字节容量-1)/3，保证转 UTF-8 后不会溢出。
 */
#define UI_LIMIT_CHARS(byte_cap) (((byte_cap) - 1) / 3)

/* 给任意金额输入框挂字符校验（只允许数字、单个小数点、最多 7 位整数 / 2 位小数） */
void    Ui_AmountEditGuard(HWND hEdit);
/* 把金额框的字符上限设好（整数 7 位 + 小数点 + 2 位小数） */
#define UI_AMOUNT_LIMIT 10

void    Ui_BuildControls(AppState* app, HWND hwnd);
void    Ui_Layout(AppState* app, HWND hwnd, int cw, int ch);
void    Ui_PaintMain(AppState* app, HWND hwnd, HDC hdc);
void    Ui_PaintPanel(AppState* app, HWND panel, HDC hdc);
int     Ui_HitTopbar(AppState* app, POINT pt);
void    Ui_ShowFileMenu(AppState* app, HWND hwnd);
/* 分类选择已改为自绘下拉框（见 menu.h 的 Ui_ComboCreate） */
void    Ui_MeasureMenuItem(MEASUREITEMSTRUCT* mis);
void    Ui_DrawMenuItem(LPDRAWITEMSTRUCT dis);
void    Ui_ApplyTheme(AppState* app);
HBRUSH  Ui_InputBrush(void);
void    Ui_DrawOwnerdrawButton(LPDRAWITEMSTRUCT dis);
LRESULT Ui_ListCustomDraw(LPNMLVCUSTOMDRAW cd);
void    Ui_RefreshTimerBar(AppState* app);
void    Ui_PaintTimerBar(AppState* app, HDC hdc);
void    Ui_ScrollAttach(HWND hList);
void    Ui_ScrollDetach(HWND hList);
void    Ui_DateEditClick(HWND hEdit);
/* 汇总卡右上角月份按钮的矩形（面板坐标），绘制与命中测试共用 */
RECT    Ui_SumMonthRect(HWND panel);
/* 记账页「支出 / 收入」分段按钮的矩形（面板坐标） */
RECT    Ui_AcctTypeRect(HWND panel);

/* ---------------- 待办四象限 ---------------- */
/* 象限名称与配色：0 重要且紧急（红）/ 1 重要不紧急（蓝）/
 * 2 紧急不重要（琥珀）/ 3 不重要不紧急（石板） */
extern const WCHAR* const kQuadName[TODO_QUAD_COUNT];
extern const char*  const kQuadNameU8[TODO_QUAD_COUNT];
/* 象限的主色（文字/圆点用），跟随浅深主题 */
COLORREF Ui_QuadColor(int q);

/* 命中测试：返回象限号（0..3），-1 = 没落在任何一块里。
 * row 返回块内的行号（含滚动偏移），-1 = 落在标题或空白处。 */
int  Ui_QuadHit(AppState* app, POINT pt, int* row);
/* 某一行在面板里的矩形（已扣掉滚动偏移，调用方保证 row 可见） */
int  Ui_QuadRowRect(AppState* app, int q, int row, RECT* out);

#endif
