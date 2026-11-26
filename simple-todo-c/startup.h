#ifndef STARTUP_H
#define STARTUP_H

/*
 * 开机自启（HKCU\Software\Microsoft\Windows\CurrentVersion\Run，值名 TodoApp）。
 *
 * 注册的命令固定带 --tray：开机静默进托盘，不弹主窗口。
 * 为什么必须自启：待办提醒靠 WM_TIMER 轮询，程序不跑就完全收不到。
 *
 * 用户的开关状态存在 config.ini 的 autostart 键（缺省 = 开）。
 * 注册表是"结果"，config 是"意图" —— 这样用户在托盘菜单里取消过后，
 * 下次启动不会被 Startup_Ensure 偷偷加回来。
 */

/* 已注册、且命令行与本 exe 路径完全一致（换了目录就算没启用） */
int  Startup_IsEnabled(void);

/* on=1 写注册表 + config；on=0 删注册表值 + config 记 0 */
void Startup_Apply(int on);

/* 启动时调一次：按 config 的意图校正注册表（首次装好自动注册、exe 挪过位置自动重写） */
void Startup_Ensure(void);

#endif
