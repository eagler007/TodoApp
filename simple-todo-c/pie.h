#ifndef PIE_H
#define PIE_H

#include <windows.h>

/* 分类占比环形图（月度报表右上角）。
 * 一次只保留一份数据：报表对话框是模态的，不存在并发。 */

#define PIE_MAX 16

/* 灌数据并重绘。names/pct/ci 三者等长，n 超过 PIE_MAX 的部分丢弃。 */
void Ui_PieSetData(const char** names, const double* pct, const int* ci,
                   int n, double total);

/* 在 parent 里开一块饼图区，返回控件句柄（失败返回 NULL） */
HWND Ui_PieCreate(HWND parent, int id, int x, int y, int w, int h);

#endif
