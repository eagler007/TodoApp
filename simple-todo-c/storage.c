#include "storage.h"
#include "util.h"
#include "cJSON.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <io.h>
#include <wchar.h>

/*
 * 路径来自 %APPDATA%，中文用户名时是 UTF-8 字节串。fopen 会按系统 ANSI 代码页
 * 解释，路径直接打不开（表现为数据永远存不下来）。一律转成 UTF-16 走 _wfopen。
 */
static int to_wide(const char* path, WCHAR* out, int cap) {
    if (!path) return 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, out, cap);
    return n > 0;
}

static FILE* open_utf8(const char* path, const WCHAR* mode) {
    WCHAR wpath[MAX_PATH * 2];
    if (!to_wide(path, wpath, MAX_PATH * 2)) return NULL;
    return _wfopen(wpath, mode);
}

static char* read_file(const char* path, long* out_size) {
    FILE* f = open_utf8(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) size = 0;
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    *out_size = (long)got;
    return buf;
}

/*
 * 原子写：先写 .tmp，落盘后再改名覆盖。直接 fopen("wb") 会立刻截断原文件，
 * 中途断电/崩溃就是一份空 JSON，整本账全丢。
 */
static int write_file(const char* path, const char* data) {
    WCHAR wpath[MAX_PATH * 2];
    WCHAR wtmp[MAX_PATH * 2];
    WCHAR wbak[MAX_PATH * 2];
    if (!to_wide(path, wpath, MAX_PATH * 2)) return 0;

    wcsncpy(wtmp, wpath, MAX_PATH * 2 - 8);
    wtmp[MAX_PATH * 2 - 8] = 0;
    wcscat(wtmp, L".tmp");

    FILE* f = _wfopen(wtmp, L"wb");
    if (!f) return 0;

    size_t len = strlen(data);
    size_t wr  = fwrite(data, 1, len, f);
    int ok = (wr == len);
    if (ok) ok = (fflush(f) == 0);
    if (ok) ok = (_commit(_fileno(f)) == 0);   /* 数据真正落到盘上再改名 */
    fclose(f);

    if (!ok) { _wremove(wtmp); return 0; }

    /* 旧文件留一份 .bak，JSON 万一手工改坏还能回滚 */
    wcsncpy(wbak, wpath, MAX_PATH * 2 - 8);
    wbak[MAX_PATH * 2 - 8] = 0;
    wcscat(wbak, L".bak");
    CopyFileW(wpath, wbak, FALSE);

    if (!MoveFileExW(wtmp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        _wremove(wtmp);
        return 0;
    }
    return 1;
}

static cJSON* todo_to_json(const Todo* t) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "id", t->id);
    cJSON_AddStringToObject(j, "title", t->title);
    cJSON_AddStringToObject(j, "content", t->content);
    cJSON_AddBoolToObject(j, "completed", t->completed);
    cJSON_AddStringToObject(j, "created_at", t->created_at);
    cJSON_AddStringToObject(j, "reminder", t->reminder);
    cJSON_AddNumberToObject(j, "repeat", t->repeat);
    cJSON_AddNumberToObject(j, "repeat_day", t->repeat_day);
    cJSON_AddNumberToObject(j, "priority", t->priority);
    /* 四象限改造后的标记：老文件没有这个键，读的时候按三档语义迁移一次 */
    cJSON_AddBoolToObject(j, "prio4", 1);
    cJSON_AddNumberToObject(j, "deleted", t->deleted);
    cJSON_AddStringToObject(j, "deleted_at", t->deleted_at);
    return j;
}

static void todo_from_json(cJSON* j, Todo* t) {
    todo_init(t);
    cJSON* id_item = cJSON_GetObjectItem(j, "id");
    if (id_item) t->id = id_item->valueint;
    cJSON* val;
    val = cJSON_GetObjectItem(j, "title");
    if (val && val->valuestring) strncpy(t->title, val->valuestring, TODO_TITLE_MAX - 1);
    val = cJSON_GetObjectItem(j, "content");
    if (val && val->valuestring) strncpy(t->content, val->valuestring, TODO_CONTENT_MAX - 1);
    val = cJSON_GetObjectItem(j, "completed");
    if (val) t->completed = val->valueint;
    val = cJSON_GetObjectItem(j, "created_at");
    if (val && val->valuestring) strncpy(t->created_at, val->valuestring, TODO_DATE_STR_LEN - 1);
    val = cJSON_GetObjectItem(j, "reminder");
    if (val && val->valuestring) strncpy(t->reminder, val->valuestring, TODO_DATE_STR_LEN - 1);
    val = cJSON_GetObjectItem(j, "repeat");
    if (val) t->repeat = val->valueint;
    val = cJSON_GetObjectItem(j, "repeat_day");
    if (val) t->repeat_day = val->valueint;
    val = cJSON_GetObjectItem(j, "priority");
    if (val) t->priority = val->valueint;
    /*
     * 老文件是三档语义：0 不重要 / 1 重要 / 2 紧急。
     * 现在的四象限里「紧急」单独一格反而是次要的，老的「紧急」其实是最优先那档，
     * 所以搬一次：2 -> 3（重要且紧急）。带 prio4 标记的新文件原样使用，不会被搬第二次。
     */
    if (!cJSON_GetObjectItem(j, "prio4") && t->priority == 2)
        t->priority = TODO_PRIO_BOTH;
    if (t->priority < TODO_PRIO_NONE) t->priority = TODO_PRIO_NONE;
    if (t->priority > TODO_PRIO_MAX)  t->priority = TODO_PRIO_MAX;
    val = cJSON_GetObjectItem(j, "deleted");
    if (val) t->deleted = val->valueint;
    val = cJSON_GetObjectItem(j, "deleted_at");
    if (val && val->valuestring) strncpy(t->deleted_at, val->valuestring, TODO_DATE_STR_LEN - 1);
}

/* 文件存在但读不出来 → -1；文件不存在 → 0。两者绝不能混为一谈：
 * 混在一起就会出现「JSON 坏了 → 当成空数据 → 一保存把原文件覆盖」 */
static int file_exists_utf8(const char* path) {
    WCHAR wpath[MAX_PATH * 2];
    if (!to_wide(path, wpath, MAX_PATH * 2)) return 0;
    DWORD attr = GetFileAttributesW(wpath);
    return attr != INVALID_FILE_ATTRIBUTES;
}

int storage_load_todos(const char* filepath, TodoList* list) {
    long size;
    char* buf = read_file(filepath, &size);
    if (!buf) return file_exists_utf8(filepath) ? -1 : 0;
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        Todo t;
        todo_from_json(item, &t);
        todo_list_add(list, &t);
    }
    cJSON_Delete(root);
    return 1;
}

int storage_save_todos(const char* filepath, TodoList* list) {
    cJSON* root = cJSON_CreateArray();
    for (int i = 0; i < list->count; i++) {
        cJSON* j = todo_to_json(&list->items[i]);
        cJSON_AddItemToArray(root, j);
    }
    char* str = cJSON_Print(root);
    int ret = write_file(filepath, str);
    free(str);
    cJSON_Delete(root);
    return ret;
}

static cJSON* acct_to_json(const AccountRecord* r) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "id", r->id);
    cJSON_AddStringToObject(j, "record_type", r->record_type);
    cJSON_AddNumberToObject(j, "amount", r->amount);
    cJSON_AddStringToObject(j, "category", r->category);
    cJSON_AddStringToObject(j, "date", r->date);
    cJSON_AddStringToObject(j, "note", r->note);
    cJSON_AddStringToObject(j, "created_at", r->created_at);
    cJSON_AddNumberToObject(j, "deleted", r->deleted);
    cJSON_AddStringToObject(j, "deleted_at", r->deleted_at);
    return j;
}

static void acct_from_json(cJSON* j, AccountRecord* r) {
    acct_init(r);
    cJSON* val;
    val = cJSON_GetObjectItem(j, "id");
    if (val) r->id = val->valueint;
    val = cJSON_GetObjectItem(j, "record_type");
    if (val && val->valuestring) strncpy(r->record_type, val->valuestring, ACCT_TYPE_LEN - 1);
    val = cJSON_GetObjectItem(j, "amount");
    if (val) r->amount = val->valuedouble;
    val = cJSON_GetObjectItem(j, "category");
    if (val && val->valuestring) strncpy(r->category, val->valuestring, ACCT_CAT_MAX - 1);
    val = cJSON_GetObjectItem(j, "date");
    if (val && val->valuestring) {
        strncpy(r->date, val->valuestring, ACCT_DATE_LEN - 1);
        normalize_date(r->date);
    }
    val = cJSON_GetObjectItem(j, "note");
    if (val && val->valuestring) strncpy(r->note, val->valuestring, ACCT_NOTE_MAX - 1);
    val = cJSON_GetObjectItem(j, "created_at");
    if (val && val->valuestring) strncpy(r->created_at, val->valuestring, ACCT_TIME_LEN - 1);
    val = cJSON_GetObjectItem(j, "deleted");
    if (val) r->deleted = val->valueint;
    val = cJSON_GetObjectItem(j, "deleted_at");
    if (val && val->valuestring) strncpy(r->deleted_at, val->valuestring, ACCT_TIME_LEN - 1);
}

int storage_load_accounts(const char* filepath, AccountList* list) {
    long size;
    char* buf = read_file(filepath, &size);
    if (!buf) return file_exists_utf8(filepath) ? -1 : 0;
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        AccountRecord r;
        acct_from_json(item, &r);
        acct_list_add(list, &r);
    }
    cJSON_Delete(root);
    return 1;
}

int storage_save_accounts(const char* filepath, AccountList* list) {
    cJSON* root = cJSON_CreateArray();
    for (int i = 0; i < list->count; i++) {
        cJSON* j = acct_to_json(&list->items[i]);
        cJSON_AddItemToArray(root, j);
    }
    char* str = cJSON_Print(root);
    int ret = write_file(filepath, str);
    free(str);
    cJSON_Delete(root);
    return ret;
}

/* ---------------- 预算 ---------------- */

int storage_load_budgets(const char* filepath, BudgetList* list) {
    long size;
    char* buf = read_file(filepath, &size);
    if (!buf) return file_exists_utf8(filepath) ? -1 : 0;
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return -1; }

    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        cJSON* cat = cJSON_GetObjectItem(item, "category");
        cJSON* amt = cJSON_GetObjectItem(item, "amount");
        if (cat && cat->valuestring && amt)
            budget_list_set(list, cat->valuestring, amt->valuedouble);
    }
    cJSON_Delete(root);
    return 1;
}

int storage_save_budgets(const char* filepath, BudgetList* list) {
    cJSON* root = cJSON_CreateArray();
    for (int i = 0; i < list->count; i++) {
        cJSON* j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "category", list->items[i].category);
        cJSON_AddNumberToObject(j, "amount", list->items[i].amount);
        cJSON_AddItemToArray(root, j);
    }
    char* str = cJSON_Print(root);
    int ret = write_file(filepath, str);
    free(str);
    cJSON_Delete(root);
    return ret;
}

/* ---------------- 按天备份 ---------------- */

int storage_daily_backup(const char* datafile, const char* backup_dir, int keep_days) {
    if (!datafile || !backup_dir || !backup_dir[0]) return 0;

    /* 文件名去扩展名当主干：accounts.json → accounts */
    const char* base = datafile;
    for (const char* p = datafile; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    char stem[128];
    strncpy(stem, base, sizeof(stem) - 1);
    stem[sizeof(stem) - 1] = 0;
    char* dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    if (!stem[0]) return 0;

    WCHAR wdir[MAX_PATH * 2];
    if (!to_wide(backup_dir, wdir, MAX_PATH * 2)) return 0;
    CreateDirectoryW(wdir, NULL);        /* 已存在会失败，忽略即可 */

    char today[16];
    get_today(today, sizeof(today));     /* YYYY/MM/DD */
    char ymd[16];
    int k = 0;
    for (int i = 0; today[i] && k < 15; i++)
        if (today[i] != '/' && today[i] != '-') ymd[k++] = today[i];
    ymd[k] = 0;

    char dst[MAX_PATH * 2];
    snprintf(dst, sizeof(dst), "%s\\%s-%s.json", backup_dir, stem, ymd);
    WCHAR wdst[MAX_PATH * 2], wsrc[MAX_PATH * 2];
    if (!to_wide(dst, wdst, MAX_PATH * 2)) return 0;
    if (GetFileAttributesW(wdst) != INVALID_FILE_ATTRIBUTES) return 0;  /* 今天已经备过 */
    if (!to_wide(datafile, wsrc, MAX_PATH * 2)) return 0;

    int made = CopyFileW(wsrc, wdst, TRUE) ? 1 : 0;

    /* 清掉 keep_days 天前的旧备份 */
    if (keep_days > 0) {
        char cutoff[16];
        time_t t = time(NULL) - (time_t)keep_days * 86400;
        struct tm* tm_c = localtime(&t);
        strftime(cutoff, sizeof(cutoff), "%Y%m%d", tm_c);

        char pat[MAX_PATH * 2];
        snprintf(pat, sizeof(pat), "%s\\%s-*.json", backup_dir, stem);
        WCHAR wpat[MAX_PATH * 2];
        if (to_wide(pat, wpat, MAX_PATH * 2)) {
            WIN32_FIND_DATAW fd;
            HANDLE hf = FindFirstFileW(wpat, &fd);
            if (hf != INVALID_HANDLE_VALUE) {
                do {
                    char nm[512];
                    wchar_to_utf8(fd.cFileName, nm, sizeof(nm));
                    char* dash = strrchr(nm, '-');
                    if (!dash) continue;
                    char d8[9] = {0};
                    strncpy(d8, dash + 1, 8);
                    if (strlen(d8) != 8) continue;
                    if (strcmp(d8, cutoff) >= 0) continue;   /* 还在保留期内 */

                    WCHAR wfull[MAX_PATH * 2];
                    char full[MAX_PATH * 2];
                    snprintf(full, sizeof(full), "%s\\%s", backup_dir, nm);
                    if (to_wide(full, wfull, MAX_PATH * 2))
                        DeleteFileW(wfull);
                } while (FindNextFileW(hf, &fd));
                FindClose(hf);
            }
        }
    }
    return made;
}

/* ---------------- CSV 导出 ---------------- */

static void csv_field(FILE* f, const char* s) {
    if (!s) s = "";
    int need = 0;
    for (const char* p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { need = 1; break; }
    }
    if (!need) { fputs(s, f); return; }
    fputc('"', f);
    for (const char* p = s; *p; p++) {
        if (*p == '"') fputc('"', f);      /* CSV 里引号自己转义成两个 */
        fputc(*p, f);
    }
    fputc('"', f);
}

int storage_export_accounts_csv(const char* path, AccountList* list, int include_deleted) {
    FILE* f = open_utf8(path, L"wb");
    if (!f) return -1;

    fputs("\xEF\xBB\xBF", f);              /* BOM：Excel 才认 UTF-8 */
    fputs("日期,类型,分类,金额,备注,状态\n", f);

    int n = 0;
    for (int i = 0; i < list->count; i++) {
        const AccountRecord* r = &list->items[i];
        if (r->deleted && !include_deleted) continue;
        char amt[32];
        snprintf(amt, sizeof(amt), "%.2f", r->amount);
        csv_field(f, r->date);
        fputc(',', f);
        fputs(acct_is_income(r) ? "收入" : "支出", f);
        fputc(',', f);
        csv_field(f, r->category);
        fputc(',', f);
        fputs(amt, f);
        fputc(',', f);
        csv_field(f, r->note);
        fputc(',', f);
        fputs(r->deleted ? "已删除" : "正常", f);
        fputc('\n', f);
        n++;
    }
    fclose(f);
    return n;
}
