#ifndef STORAGE_H
#define STORAGE_H

#include "todo.h"
#include "account.h"

/* 加载返回值：1 = 成功；0 = 文件不存在（首次运行）；-1 = 文件存在但解析失败
 * （调用方必须停下来，否则会拿空列表把原文件覆盖掉） */
int storage_load_todos(const char* filepath, TodoList* list);
int storage_save_todos(const char* filepath, TodoList* list);
int storage_load_accounts(const char* filepath, AccountList* list);
int storage_save_accounts(const char* filepath, AccountList* list);

int storage_load_budgets(const char* filepath, BudgetList* list);
int storage_save_budgets(const char* filepath, BudgetList* list);

/* 按天留一份备份：backup_dir/<stem>-YYYYMMDD.json，已存在则跳过；
 * 顺手清掉 keep_days 天前的旧备份。返回 1 表示这次新建了备份 */
int storage_daily_backup(const char* datafile, const char* backup_dir, int keep_days);

/* 导出记账明细 CSV（UTF-8 带 BOM，Excel 直接双击不乱码），返回导出的记录数 */
int storage_export_accounts_csv(const char* path, AccountList* list, int include_deleted);

#endif
