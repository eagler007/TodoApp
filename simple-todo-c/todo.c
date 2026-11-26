#include "todo.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void todo_quad_sub(const Todo* t, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!t) return;

    /* 提醒只显示到分钟（"2026-10-25T09:00:00" -> "10-25 09:00"），四象限卡片一行放不下完整时间 */
    if (t->reminder[0] && strlen(t->reminder) >= 16) {
        const char* p = t->reminder;
        if (p[4] == '-')
            snprintf(out, (size_t)cap, "%.5s %.5s", p + 5, p + 11);
        else
            snprintf(out, (size_t)cap, "%s", p);
    }
}

void todo_init(Todo* t) {
    memset(t, 0, sizeof(Todo));
}

void todo_list_init(TodoList* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void todo_list_free(TodoList* list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int todo_list_grow(TodoList* list) {
    int new_cap = list->capacity ? list->capacity * 2 : 16;
    Todo* new_items = (Todo*)realloc(list->items, new_cap * sizeof(Todo));
    if (!new_items) return 0;
    list->items = new_items;
    list->capacity = new_cap;
    return 1;
}

int todo_list_add(TodoList* list, const Todo* t) {
    if (list->count >= list->capacity && !todo_list_grow(list))
        return 0;
    list->items[list->count] = *t;
    list->count++;
    return 1;
}

int todo_list_remove(TodoList* list, int id) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id == id) {
            if (i < list->count - 1)
                memmove(&list->items[i], &list->items[i + 1],
                        (list->count - i - 1) * sizeof(Todo));
            list->count--;
            return 1;
        }
    }
    return 0;
}

Todo* todo_list_find(TodoList* list, int id) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].id == id)
            return &list->items[i];
    }
    return NULL;
}

static int todo_compare_date(const void* a, const void* b) {
    const Todo* ta = (const Todo*)a;
    const Todo* tb = (const Todo*)b;
    return strcmp(tb->created_at, ta->created_at);
}

void todo_list_sort_by_date(TodoList* list) {
    qsort(list->items, list->count, sizeof(Todo), todo_compare_date);
}

int todo_list_soft_delete(TodoList* list, int id) {
    Todo* t = todo_list_find(list, id);
    if (!t) return 0;
    t->deleted = 1;
    get_now_str(t->deleted_at, sizeof(t->deleted_at));
    return 1;
}

int todo_list_restore(TodoList* list, int id) {
    Todo* t = todo_list_find(list, id);
    if (!t) return 0;
    t->deleted = 0;
    t->deleted_at[0] = 0;
    return 1;
}

int todo_list_purge_deleted(TodoList* list, int keep_days) {
    char cutoff[TODO_DATE_STR_LEN];
    time_t t = time(NULL) - (time_t)keep_days * 86400;
    struct tm* tm_c = localtime(&t);
    strftime(cutoff, sizeof(cutoff), "%Y-%m-%dT%H:%M:%S", tm_c);

    int removed = 0;
    for (int i = list->count - 1; i >= 0; i--) {
        if (!list->items[i].deleted) continue;
        if (list->items[i].deleted_at[0] && strcmp(list->items[i].deleted_at, cutoff) < 0) {
            int id = list->items[i].id;
            if (todo_list_remove(list, id)) removed++;
        }
    }
    return removed;
}
