#ifndef TREND_H
#define TREND_H

#include <windows.h>

#define TREND_MAX 12

/* 月份趋势折线图：画在对话框里的子窗口，数据由调用方按时间升序喂进来。
 * 只做展示，不持有业务数据。 */
HWND Ui_TrendCreate(HWND parent, int id, int x, int y, int w, int h);

/* labels: 每个点的横轴短标签（如 "10"）；vals: 金额；n <= TREND_MAX；
 * hi: 需要重点标注（画实心点 + 金额气泡）的下标，-1 表示不标 */
void Ui_TrendSetData(const char** labels, const double* vals, int n, int hi);

#endif
