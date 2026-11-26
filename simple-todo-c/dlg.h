#ifndef DLG_H
#define DLG_H

#include <windows.h>

/*
 * 自绘模态对话框框架。
 * 原生 DialogBox + 模板在深色主题下会是一块白板（Win10 1809 无深色控件支持），
 * 所以这里用普通窗口 + 自绘标题栏重做，控件也走自绘。
 *
 * 所有 Ui_Dlg* 的坐标都是「逻辑像素」，内部按 DPI 缩放。
 * 客户区顶部 UI_DLG_TITLE_H 是标题栏，控件请从它下面开始排。
 */

#define UI_DLG_TITLE_H 40

/* 对话框内的统一间距（逻辑像素）。四个对话框的按钮行/内边距全部按这套来，
   免得这个 80 宽那个 98 宽、这个距底 14 那个距底 20。 */
#define DLG_PAD        20   /* 内容区左右边距 */
#define DLG_BTN_H      34
#define DLG_BTN_W_OK   96
#define DLG_BTN_W_NO   80
#define DLG_BTN_GAP    12
#define DLG_BTN_BOTTOM 20   /* 按钮距对话框底边 */

/* 按统一规格算出「取消 / 确定」两个按钮的 x 与 y（w/h = 对话框尺寸） */
#define DLG_OK_X(w)      ((w) - DLG_PAD - DLG_BTN_W_OK)
#define DLG_CANCEL_X(w)  (DLG_OK_X(w) - DLG_BTN_GAP - DLG_BTN_W_NO)
#define DLG_BTN_Y(h)     ((h) - DLG_BTN_BOTTOM - DLG_BTN_H)

typedef int (*UiDlgProc)(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);

/* 创建对话框窗口（不显示）；w/h 含标题栏 */
HWND Ui_DlgBegin(HWND owner, const WCHAR* title, int w, int h, UiDlgProc proc);
/* 跑模态循环，返回 1 = 确定，0 = 取消/关闭；返回后 hDlg 已销毁 */
int  Ui_DlgRun(HWND hDlg);
/* 由窗口内部或回调调用，结束对话框 */
void Ui_DlgFinish(HWND hDlg, int result);

HWND Ui_DlgLabel(HWND d, const WCHAR* text, int x, int y, int w, int h);
HWND Ui_DlgEdit(HWND d, int id, int x, int y, int w, int h, int multiline);
/* 同上，文本水平居中 */
HWND Ui_DlgEditCenter(HWND d, int id, int x, int y, int w, int h);
HWND Ui_DlgButton(HWND d, int id, const WCHAR* text, int x, int y, int w, int h, int primary);
HWND Ui_DlgList(HWND d, int id, int x, int y, int w, int h);
/* 日期框：文本框 + 小日历图标，单击弹自绘日历 */
HWND Ui_DlgDate(HWND d, int id, int x, int y, int w, int h);
/* 同上，日期文字水平居中 */
HWND Ui_DlgDateCenter(HWND d, int id, int x, int y, int w, int h);
/* 月份框：同上，但弹出的是 12 个月份格（值格式 "YYYY-MM"） */
HWND Ui_DlgMonth(HWND d, int id, int x, int y, int w, int h);

/* 时间框：点击弹「时 / 分」两列选择器，禁止手动输入 */
HWND Ui_DlgTime(HWND d, int id, int x, int y, int w, int h);

/* 给对话框内控件套用主题（在 Ui_DlgBegin 里自动调用，重建控件后可再调一次） */
void Ui_DlgSkin(HWND hDlg);

/* 自绘消息框，替代 MessageBoxW。返回 1 = 确定，0 = 取消/关闭 */
#define MSG_INFO  0
#define MSG_WARN  1
#define MSG_ERROR 2
#define MSG_YESNO 3
int Ui_MsgBox(HWND owner, const WCHAR* title, const WCHAR* text, int kind);

/* 后台提醒用：不碰主窗口（不恢复、不激活），只把自己置顶 + 抢前台 + 闪边框。
   倒计时到点用它 —— 主窗口最小化/藏托盘时只弹这一张提示框。 */
int Ui_MsgBoxFront(HWND owner, const WCHAR* title, const WCHAR* text, int kind);

#endif
