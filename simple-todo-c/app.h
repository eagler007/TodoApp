#ifndef APP_H
#define APP_H

#include <windows.h>
#include <commctrl.h>
#include "todo.h"
#include "account.h"

#define APP_NAME L"TodoApp"
#define APP_TITLE L"TodoApp"
#define TODO_PANEL_CLASS L"TodoPanel"
#define APP_WIDTH 720
#define APP_HEIGHT 700
#define APP_MIN_WIDTH 680
#define APP_MIN_HEIGHT 460

/* 每个象限最多显示多少条（超出的用「还有 N 条」提示，不影响数据） */
#define QUAD_MAX_ROWS 128

#define ID_TAB        100
#define ID_TODO_LIST  101
#define ID_ACCT_LIST  102
#define ID_TODO_TITLE 103
#define ID_TODO_ADD   104
#define ID_TODO_SEARCH 105
#define ID_ACCT_AMOUNT 106
#define ID_ACCT_CATEGORY 107
#define ID_ACCT_DATE  108
#define ID_ACCT_NOTE  109
#define ID_ACCT_ADD   110
#define ID_ACCT_SEARCH 111
#define ID_ACCT_MONTHLY 113
#define ID_STAT_DETAIL  301   /* 统计报表里的「明细」按钮 */
#define ID_TODO_CONTENT 114
#define ID_ACCT_CAT_COMBO 115
#define ID_STATUS_CLOCK 116
#define ID_TRAY_ICON 1001
#define ID_TRAY_SHOW 2001
#define ID_TRAY_EXIT 2002
#define ID_TRAY_AUTOSTART 2003   /* 托盘菜单里的「开机自启」勾选项 */
#define ID_TODO_REFRESH 117
#define ID_ACCT_REFRESH 118
#define ID_ACCT_CAT_DROP 126
#define ID_MENU_BACKUP 3001
#define ID_MENU_IMPORT 3002
#define ID_MENU_EXPORT_CSV 3003
#define ID_MENU_TRASH 3004
#define ID_MENU_BUDGET 3005
/* 编辑待办对话框里的四个优先级按钮（顺序 = 重要且紧急 / 重要不紧急 / 紧急不重要 / 不重要不紧急） */
#define ID_TODO_PRIO_BOTH      133
#define ID_TODO_PRIO_IMPORTANT 129
#define ID_TODO_PRIO_URGENT    128
#define ID_TODO_PRIO_NONE      130
/* 主界面「优先级」下拉框（四象限） */
#define ID_TODO_PRIO_COMBO 132
/* 主界面添加区的优先级分段按钮（画在面板上，靠命中测试，不用子控件） */
#define ID_TRASH_LIST 401
#define ID_TRASH_RESTORE 402
#define ID_TRASH_PURGE 403
#define ID_TRASH_EMPTY 404
#define ID_BUDGET_LIST 501
#define ID_BUDGET_EDIT 502
#define ID_BUDGET_HINT 503
#define ID_BUDGET_CAT  504
/* 3006 起留给后续新增的文件菜单功能 */

#define ID_TMR_HOUR    119
#define ID_TMR_MIN     120
#define ID_TMR_SEC     121
#define ID_TMR_START   122
#define ID_TMR_PAUSE   123
#define ID_TMR_RESET   124
#define ID_TMR_DISPLAY 125

#define TRAY_MSG (WM_APP + 1)
#define TIMER_CLOCK 1
#define TIMER_COUNTDOWN 2
#define TIMER_REMINDER 3

/* 顶部栏热区编号 */
#define HIT_NONE  0
#define HIT_FILE  1
#define HIT_THEME 2
#define HIT_TAB0  3
#define HIT_TAB1  4
#define HIT_MIN   5
#define HIT_MAX   6
#define HIT_CLOSE 7
#define HIT_WEATHER 8      /* 城市 + 气温，双击改城市 */

/* 天气线程拿到结果后 PostMessage 给主窗口（wParam = 1 成功 / 0 失败） */
#define WM_APP_WEATHER (WM_APP + 21)
/* 窗口起来之后补报「程序没开时错过的提醒」。
   单独走一条消息而不是在 WM_CREATE 里直接弹模态框，
   否则创建流程会被 Ui_DlgRun 自己的消息循环卡住。 */
#define WM_APP_REMINDER_BOOT (WM_APP + 22)

typedef struct {
    TodoList todos;
    AccountList accounts;
    BudgetList budgets;      /* 分类月预算 */

    /* 顶部栏 / 倒计时栏自绘热区 */
    RECT rt_brand;
    RECT rt_file;
    RECT rt_weather;                            /* 文件与页签之间的「城市 气象 温度」 */
    RECT rt_clock;                              /* 页签左侧的日期时钟 */
    int  weather_compact;                       /* 1 = 宽度不够，只显示「城市 温度」 */
    RECT rt_tab0;
    RECT rt_tab1;
    RECT rt_theme;
    RECT rt_min;
    RECT rt_max;
    RECT rt_close;
    int  hover_hit;
    int  pressed_hit;

    HWND hTodoPanel;
    HWND hAcctPanel;
    HWND hTimerBar;

    HWND hTodoTitle;
    HWND hTodoContent;
    HWND hTodoSearch;
    HWND hTodoPrioCombo;      /* 优先级下拉框（四象限，带色点） */

    /* ---- 待办四象限视图（不再用 ListView，整块自绘） ---- */
    RECT rt_quad[TODO_QUAD_COUNT];              /* 每块矩形（面板客户区坐标） */
    int  quad_ids[TODO_QUAD_COUNT][QUAD_MAX_ROWS];  /* 每块当前显示的待办 id */
    int  quad_n[TODO_QUAD_COUNT];               /* 已填入的行数（<= QUAD_MAX_ROWS） */
    int  quad_total[TODO_QUAD_COUNT];           /* 该象限实际条数（超过上限时用来显示 +N） */
    int  quad_scroll[TODO_QUAD_COUNT];          /* 每块滚过的行数 */
    int  quad_rows_vis;                         /* 每块一屏能显示几行（Layout 算好，绘制/命中共用） */
    int  quad_hover;                            /* 悬停的象限号，-1 = 无 */
    int  quad_hover_row;                        /* 悬停的行号，-1 = 无 */
    int  quad_sel_id;                           /* 选中的待办 id，0 = 无 */
    /* 双击兜底：系统的 WM_LBUTTONDBLCLK 并不可靠（窗口刚激活 / 输入队列被占时
       会退化成两次普通 WM_LBUTTONDOWN，表现就是「第一次双击没反应」）。
       自己按 时间+位置+同一行 记一次点击，够条件就直接当双击处理。 */
    int    quad_last_click_id;
    DWORD  quad_last_click_tick;
    POINT  quad_last_click_pt;

    HWND hAcctAmount;
    HWND hAcctCategory;
    HWND hAcctDate;
    HWND hAcctNote;
    HWND hAcctSearch;
    HWND hAcctCatCombo;   /* 自绘下拉框，不再配独立的箭头按钮 */
    HWND hAcctList;

    HWND hTimerHour;
    HWND hTimerMin;
    HWND hTimerSec;
    HWND hTimerDisplay;

    HINSTANCE hInst;
    int current_tab;
    int next_todo_id;
    int next_acct_id;
    WCHAR data_dir[MAX_PATH];
    WCHAR todo_path[MAX_PATH];
    WCHAR acct_path[MAX_PATH];
    WCHAR config_path[MAX_PATH];
    WCHAR budget_path[MAX_PATH];
    WCHAR backup_dir[MAX_PATH];      /* <data_dir>\backups */
    WCHAR clock_text[64];
    int initialized;

    /* 记账录入的收支类型：0 = 支出，1 = 收入 */
    int acct_type;
    int hover_acct_type;             /* 分段按钮 hover：0 无 / 1 支出 / 2 收入 */
    /* 新增待办时选的优先级（TODO_PRIO_*），加完不清空，方便连续录同类事项 */
    int todo_prio;
    /* 数据文件损坏时进入只读：宁可拦着不写，也不能拿空列表覆盖原文件 */
    int read_only;
    int data_broken;

    int timer_running;
    int timer_paused;
    int timer_seconds;
    int timer_total;      /* 本轮设定的总秒数，倒计时结束时用来生成提示文案 */

    /* 记账列表的月份过滤，格式 "YYYY/MM"；空串表示不按月筛（显示全部） */
    char acct_month[16];
    int  hover_sum_mon;   /* 汇总卡右上角月份按钮的 hover 态 */
} AppState;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowEditTodoDlg(HWND owner, Todo* t);
void ShowEditAcctDlg(HWND owner, AccountRecord* rec);
void ShowReminderDlg(HWND owner, Todo* t);
void ShowMonthlyDlg(HWND owner, AppState* app);

#endif
