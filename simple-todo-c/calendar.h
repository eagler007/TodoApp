#ifndef CALENDAR_H
#define CALENDAR_H

#include <windows.h>

/*
 * 自绘日期选择器（popup）。
 * owner : 所属窗口
 * anchor: 锚点控件，弹层贴其左下角弹出；为 NULL 时用屏幕坐标 (px, py)
 * cur   : 当前值，形如 "2026-10-23" / "2026/10/23"，可为 NULL 或空（取今天）
 * out   : 选中结果，格式 "YYYY-MM-DD"
 * 返回  : 1 = 选中，0 = 取消
 */
int Ui_PickDate(HWND owner, HWND anchor, int px, int py,
                const WCHAR* cur, WCHAR* out, int out_n);

/*
 * 同上的「只选年月」版本：弹层是 12 个月份格，左右箭头切年。
 * cur / out 格式 "YYYY-MM"。
 */
int Ui_PickMonth(HWND owner, HWND anchor, int px, int py,
                 const WCHAR* cur, WCHAR* out, int out_n);

/*
 * 时间选择器：左右两列（时 0-23 / 分 0-59），滚轮或箭头滚动，点分钟即确认。
 * cur / out 格式 "HH:MM"。
 */
int Ui_PickTime(HWND owner, HWND anchor, int px, int py,
                const WCHAR* cur, WCHAR* out, int out_n);

#endif
