#include "account.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 8 个标准分类：菜单、预算、统计三处共用，改这里就全改 */
const char* const kStdCategories[ACCT_STD_CAT_N] = {
    "餐饮", "购物", "交通", "生活", "娱乐", "人情", "医疗", "其他",
};

void acct_init(AccountRecord* r) {
    memset(r, 0, sizeof(AccountRecord));
    get_today(r->date, sizeof(r->date));
    get_now_str(r->created_at, sizeof(r->created_at));
    strcpy(r->record_type, "expense");
}

void acct_list_init(AccountList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void acct_list_free(AccountList* list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int acct_list_grow(AccountList* list) {
    int new_cap = list->capacity ? list->capacity * 2 : 16;
    AccountRecord* new_items = (AccountRecord*)realloc(list->items, new_cap * sizeof(AccountRecord));
    if (!new_items) return 0;
    list->items = new_items;
    list->capacity = new_cap;
    return 1;
}

int acct_list_add(AccountList* list, const AccountRecord* r) {
    if (list->count >= list->capacity && !acct_list_grow(list))
        return 0;
    list->items[list->count] = *r;
    list->count++;
    return 1;
}

int acct_list_remove(AccountList* list, int id) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id == id) {
            if (i < list->count - 1)
                memmove(&list->items[i], &list->items[i + 1],
                        (list->count - i - 1) * sizeof(AccountRecord));
            list->count--;
            return 1;
        }
    }
    return 0;
}

AccountRecord* acct_list_find(AccountList* list, int id) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id == id)
            return &list->items[i];
    }
    return NULL;
}

static int acct_compare_date_desc(const void* a, const void* b) {
    const AccountRecord* ra = (const AccountRecord*)a;
    const AccountRecord* rb = (const AccountRecord*)b;
    int cmp = strcmp(rb->date, ra->date);
    if (cmp != 0) return cmp;
    return strcmp(rb->created_at, ra->created_at);
}

void acct_list_sort_by_date_desc(AccountList* list) {
    qsort(list->items, list->count, sizeof(AccountRecord), acct_compare_date_desc);
}

int acct_is_income(const AccountRecord* r) {
    return r && strcmp(r->record_type, "income") == 0;
}

/* 软删除：只是打个标记，记录还在，「最近删除」里能捞回来 */
int acct_list_soft_delete(AccountList* list, int id) {
    AccountRecord* r = acct_list_find(list, id);
    if (!r) return 0;
    r->deleted = 1;
    get_now_str(r->deleted_at, sizeof(r->deleted_at));
    return 1;
}

int acct_list_restore(AccountList* list, int id) {
    AccountRecord* r = acct_list_find(list, id);
    if (!r) return 0;
    r->deleted = 0;
    r->deleted_at[0] = 0;
    return 1;
}

/* 超过 keep_days 天的软删除记录彻底清掉。deleted_at 是 ISO 时间串，
 * 补零后字典序即时间序，直接字符串比较就够 */
int acct_list_purge_deleted(AccountList* list, int keep_days) {
    char cutoff[ACCT_TIME_LEN];
    time_t t = time(NULL) - (time_t)keep_days * 86400;
    struct tm* tm_c = localtime(&t);
    strftime(cutoff, sizeof(cutoff), "%Y-%m-%dT%H:%M:%S", tm_c);

    int removed = 0;
    for (int i = list->count - 1; i >= 0; i--) {
        if (!list->items[i].deleted) continue;
        if (list->items[i].deleted_at[0] && strcmp(list->items[i].deleted_at, cutoff) < 0) {
            int id = list->items[i].id;
            if (acct_list_remove(list, id)) removed++;
        }
    }
    return removed;
}

double acct_list_month_sum(AccountList* list, const char* month, const char* type) {
    double total = 0;
    if (!list || !month || !month[0]) return 0;
    for (int i = 0; i < list->count; i++) {
        const AccountRecord* r = &list->items[i];
        if (r->deleted) continue;
        if (type) {
            int want_income = strcmp(type, "income") == 0;
            if (acct_is_income(r) != want_income) continue;
        }
        char im[16];
        get_month_from_date(r->date, im, sizeof(im));
        if (strcmp(im, month) == 0) total += r->amount;
    }
    return total;
}

double acct_list_category_total(AccountList* list, const char* category, const char* month) {
    double total = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].deleted) continue;
        if (strcmp(list->items[i].category, category) != 0)
            continue;
        char item_month[16];
        get_month_from_date(list->items[i].date, item_month, sizeof(item_month));
        if (strcmp(item_month, month) == 0)
            total += list->items[i].amount;
    }
    return total;
}

double acct_list_month_total(AccountList* list, const char* month) {
    return acct_list_month_sum(list, month, "expense");
}

/* ---------------- 预算 ---------------- */

void budget_list_init(BudgetList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void budget_list_free(BudgetList* list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

double budget_list_get(const BudgetList* list, const char* category) {
    if (!list || !category) return 0;
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].category, category) == 0)
            return list->items[i].amount;
    }
    return 0;
}

/* amount <= 0 表示删除该分类的预算 */
int budget_list_set(BudgetList* list, const char* category, double amount) {
    if (!list || !category) return 0;
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].category, category) != 0) continue;
        if (amount <= 0) {
            if (i < list->count - 1)
                memmove(&list->items[i], &list->items[i + 1],
                        (list->count - i - 1) * sizeof(BudgetItem));
            list->count--;
            return 1;
        }
        list->items[i].amount = amount;
        return 1;
    }
    if (amount <= 0) return 1;

    if (list->count >= list->capacity) {
        int cap = list->capacity ? list->capacity * 2 : 8;
        BudgetItem* p = (BudgetItem*)realloc(list->items, (size_t)cap * sizeof(BudgetItem));
        if (!p) return 0;
        list->items = p;
        list->capacity = cap;
    }
    memset(&list->items[list->count], 0, sizeof(BudgetItem));
    strncpy(list->items[list->count].category, category, ACCT_CAT_MAX - 1);
    list->items[list->count].amount = amount;
    list->count++;
    return 1;
}

