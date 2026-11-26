#ifndef TODO_H
#define TODO_H

#define TODO_TITLE_MAX 256
#define TODO_CONTENT_MAX 1024
#define TODO_DATE_STR_LEN 32

/* 重复周期的取值。2026-10-25 起界面不再支持重复任务（勾完就是完成，不生成下一期），
 * 这两个字段只为**兼容老数据**留着：读盘时照读、保存时照写，但不再产生任何行为。 */
#define TODO_REPEAT_NONE  0
#define TODO_REPEAT_DAILY 1
#define TODO_REPEAT_WEEKLY 2
#define TODO_REPEAT_MONTHLY 3

/* 优先级 = 艾森豪威尔四象限，数值越大越靠前（也是主界面 4 个方块的顺序倒序）
 *   3 = 重要且紧急      2 = 重要不紧急
 *   1 = 紧急不重要      0 = 不重要不紧急
 * 老版本只有三档（0 不重要 / 1 重要 / 2 紧急），读盘时按 prio4 标记做一次迁移。 */
#define TODO_PRIO_NONE      0   /* 不重要不紧急 */
#define TODO_PRIO_URGENT    1   /* 紧急不重要 */
#define TODO_PRIO_IMPORTANT 2   /* 重要不紧急 */
#define TODO_PRIO_BOTH      3   /* 重要且紧急 */
#define TODO_PRIO_MAX       3

/* 主界面四象限的格子顺序：0 = 重要且紧急 … 3 = 不重要不紧急 */
#define TODO_QUAD_COUNT 4
/* 象限号 -> priority 值 */
#define TODO_QUAD_PRIO(q) (TODO_PRIO_BOTH - (q))
/* priority 值 -> 象限号 */
#define TODO_PRIO_QUAD(p) (TODO_PRIO_BOTH - (p))

typedef struct {
    int id;
    char title[TODO_TITLE_MAX];
    char content[TODO_CONTENT_MAX];
    int completed;
    char created_at[TODO_DATE_STR_LEN];
    char reminder[TODO_DATE_STR_LEN];
    int  repeat;                       /* 历史字段，仅兼容老数据，界面已不用 */
    /* 历史字段，仅兼容老数据：每周 0..6 / 每月 1..31 */
    int  repeat_day;
    int  priority;                     /* TODO_PRIO_* */
    int  deleted;                      /* 软删除，同记账 */
    char deleted_at[TODO_DATE_STR_LEN];
} Todo;

typedef struct {
    Todo* items;
    int count;
    int capacity;
} TodoList;

void todo_init(Todo* t);
void todo_list_init(TodoList* list);
void todo_list_free(TodoList* list);
int todo_list_add(TodoList* list, const Todo* t);
int todo_list_remove(TodoList* list, int id);
Todo* todo_list_find(TodoList* list, int id);
void todo_list_sort_by_date(TodoList* list);

int  todo_list_soft_delete(TodoList* list, int id);
int  todo_list_restore(TodoList* list, int id);
int  todo_list_purge_deleted(TodoList* list, int keep_days);

/*
 * 四象限条目右侧的小字：只有提醒时间，没有就写空串。
 * 例："10-25 09:00"
 */
void todo_quad_sub(const Todo* t, char* out, int cap);

#endif
