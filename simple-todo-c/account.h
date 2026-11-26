#ifndef ACCOUNT_H
#define ACCOUNT_H

#define ACCT_CAT_MAX 64
#define ACCT_NOTE_MAX 256
#define ACCT_DATE_LEN 16
#define ACCT_TYPE_LEN 16
#define ACCT_TIME_LEN 32

typedef struct {
    int id;
    char record_type[ACCT_TYPE_LEN];   /* "expense" 支出 / "income" 收入 */
    double amount;
    char category[ACCT_CAT_MAX];
    char date[ACCT_DATE_LEN];
    char note[ACCT_NOTE_MAX];
    char created_at[ACCT_TIME_LEN];
    /* 软删除：1 = 在「最近删除」里，不再出现在列表与统计中，超过 30 天彻底清掉 */
    int  deleted;
    char deleted_at[ACCT_TIME_LEN];
} AccountRecord;

typedef struct {
    AccountRecord* items;
    int count;
    int capacity;
} AccountList;

/* 分类月预算：category 为空串的那一条是「总预算」 */
typedef struct {
    char   category[ACCT_CAT_MAX];
    double amount;
} BudgetItem;

typedef struct {
    BudgetItem* items;
    int count;
    int capacity;
} BudgetList;

void acct_init(AccountRecord* r);
int  acct_is_income(const AccountRecord* r);
void acct_list_init(AccountList* list);
void acct_list_free(AccountList* list);
int acct_list_add(AccountList* list, const AccountRecord* r);
int acct_list_remove(AccountList* list, int id);
AccountRecord* acct_list_find(AccountList* list, int id);
void acct_list_sort_by_date_desc(AccountList* list);

/* 软删除：标记 + 时间戳；restore 反向操作；purge 清掉超过 keep_days 天的 */
int  acct_list_soft_delete(AccountList* list, int id);
int  acct_list_restore(AccountList* list, int id);
int  acct_list_purge_deleted(AccountList* list, int keep_days);

/* 当月合计：type 传 "expense"/"income"，NULL 表示两支都算；一律跳过已软删除的记录 */
double acct_list_month_sum(AccountList* list, const char* month, const char* type);
double acct_list_category_total(AccountList* list, const char* category, const char* month);
double acct_list_month_total(AccountList* list, const char* month);

/* 预算 */
void   budget_list_init(BudgetList* list);
void   budget_list_free(BudgetList* list);
int    budget_list_set(BudgetList* list, const char* category, double amount);
double budget_list_get(const BudgetList* list, const char* category);

/* 8 个标准分类（餐饮/购物/交通/生活/娱乐/人情/医疗/其他），定义在 account.c */
#define ACCT_STD_CAT_N 8
extern const char* const kStdCategories[ACCT_STD_CAT_N];

#endif
