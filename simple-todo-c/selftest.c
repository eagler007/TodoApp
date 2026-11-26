/*
 * 自检程序：不弹界面，跑一遍纯逻辑（日期、收支、软删除、预算、存储往返、CSV）。
 * 每次改完代码跑一次，出问题立刻能看见，不用等界面上发现数据没了。
 *
 * 编译：sh _build_selftest.sh   运行：TodoSelfTest.exe
 */
#include "util.h"
#include "account.h"
#include "todo.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, what)                                            \
    do {                                                             \
        if (cond) { g_pass++; }                                       \
        else { g_fail++; printf("  FAIL  %s\n", what); }               \
    } while (0)

/* 删掉通配匹配到的文件（只在 %TEMP% 的测试目录里用，保证用例可重复运行） */
static void wipe_matching(const char* pattern) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char* slash = strrchr(pattern, '\\');
        if (!slash) slash = strrchr(pattern, '/');
        if (!slash) break;
        int n = (int)(slash - pattern) + 1;
        char full[MAX_PATH * 4];
        snprintf(full, sizeof(full), "%.*s%s", n, pattern, fd.cFileName);
        DeleteFileA(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void test_date(void) {
    printf("[date]\n");
    /* 存储里的日期一律是 YYYY/MM/DD，别想当然写成 YYYY-MM-DD */
    CHECK(is_valid_date("2026/10/24"), "2026/10/24 should be valid");
    CHECK(is_valid_date("2024/02/29"), "2024/02/29 (leap year) should be valid");
    CHECK(!is_valid_date("2026/02/29"), "2026/02/29 is not a leap day");
    CHECK(!is_valid_date("2026/13/01"), "month 13 must be rejected");
    CHECK(!is_valid_date("2026/00/10"), "month 0 must be rejected");
    CHECK(!is_valid_date("2026-10-24"), "hyphen form is NOT the storage format");
    CHECK(!is_valid_date("abc"), "garbage must be rejected");

    char d[16];
    strcpy(d, "2026-10-5");
    normalize_date(d);
    CHECK(strcmp(d, "2026/10/5") == 0, "normalize hyphen -> slash");

    char m[16];
    get_month_from_date("2026-10-24", m, sizeof(m));
    CHECK(strcmp(m, "2026-10") == 0, "month of 2026-10-24 is 2026-10");
    get_month_from_date("2026/3/7", m, sizeof(m));
    CHECK(strcmp(m, "2026-03") == 0, "month of 2026/3/7 is 2026-03");
}

/* 「每月 N 号」撞上小月 / 闰年怎么处理 —— 这是老板专门问过的点，用例得钉死。
 * 规则：当月没有这一天就取月末；算出来不晚于今天则整体再往后一个月。 */
static void test_month_len(void) {
    printf("[month]\n");
    CHECK(Util_DaysInMonth(2026, 2) == 28, "2026-02 has 28 days");
    CHECK(Util_DaysInMonth(2028, 2) == 29, "2028-02 has 29 days (leap)");
    CHECK(Util_DaysInMonth(2100, 2) == 28, "2100-02 has 28 days (century, not leap)");
    CHECK(Util_DaysInMonth(2000, 2) == 29, "2000-02 has 29 days (400-year rule)");
    CHECK(Util_DaysInMonth(2026, 4) == 30, "2026-04 has 30 days");
    CHECK(Util_DaysInMonth(2026, 12) == 31, "2026-12 has 31 days");
    CHECK(Util_DaysInMonth(2026, 13) == 0, "month 13 has no days");
    CHECK(Util_DaysInMonth(2026, 0) == 0, "month 0 has no days");
}

static void test_account(void) {
    printf("[account]\n");
    AccountRecord r;
    acct_init(&r);
    CHECK(!acct_is_income(&r), "default record_type is expense");
    strcpy(r.record_type, "income");
    CHECK(acct_is_income(&r), "record_type=income detected");

    AccountList list;
    acct_list_init(&list);

    AccountRecord a;
    acct_init(&a);
    a.id = 1; a.amount = 100.0;
    strcpy(a.category, "餐饮");
    strcpy(a.date, "2026-10-01");
    acct_list_add(&list, &a);

    AccountRecord b;
    acct_init(&b);
    b.id = 2; b.amount = 50.0;
    strcpy(b.record_type, "income");
    strcpy(b.category, "工资");
    strcpy(b.date, "2026-10-02");
    acct_list_add(&list, &b);

    CHECK(acct_list_month_sum(&list, "2026-10", "expense") == 100.0, "expense sum = 100");
    CHECK(acct_list_month_sum(&list, "2026-10", "income") == 50.0, "income sum = 50");
    CHECK(acct_list_month_sum(&list, "2026-10", NULL) == 150.0, "total sum = 150");
    CHECK(acct_list_month_total(&list, "2026-10") == 100.0, "month_total only counts expense");

    /* 软删除之后统计必须跟着变 */
    acct_list_soft_delete(&list, 1);
    CHECK(acct_list_month_sum(&list, "2026-10", "expense") == 0.0, "deleted row excluded from sum");
    CHECK(acct_list_find(&list, 1)->deleted == 1, "deleted flag set");
    CHECK(acct_list_find(&list, 1)->deleted_at[0] != 0, "deleted_at stamped");

    acct_list_restore(&list, 1);
    CHECK(acct_list_month_sum(&list, "2026-10", "expense") == 100.0, "restore brings it back");

    /* 保留期清理：刚删的不该被清掉 */
    acct_list_soft_delete(&list, 1);
    CHECK(acct_list_purge_deleted(&list, 30) == 0, "fresh deleted row survives purge");
    CHECK(acct_list_find(&list, 1) != NULL, "row still there");

    /* 手工把时间戳改成很早以前，应当被清掉 */
    strcpy(acct_list_find(&list, 1)->deleted_at, "2000-01-01T00:00:00");
    CHECK(acct_list_purge_deleted(&list, 30) == 1, "old deleted row purged");
    CHECK(acct_list_find(&list, 1) == NULL, "row gone after purge");

    acct_list_free(&list);
}

static void test_budget(void) {
    printf("[budget]\n");
    BudgetList b;
    budget_list_init(&b);
    CHECK(budget_list_get(&b, "餐饮") == 0, "unset budget reads 0");

    budget_list_set(&b, "餐饮", 1500);
    CHECK(budget_list_get(&b, "餐饮") == 1500, "budget set to 1500");

    budget_list_set(&b, "餐饮", 1800);
    CHECK(budget_list_get(&b, "餐饮") == 1800, "budget overwritten to 1800");
    CHECK(b.count == 1, "no duplicate row on overwrite");

    budget_list_set(&b, "餐饮", 0);
    CHECK(budget_list_get(&b, "餐饮") == 0, "0 clears the budget");
    CHECK(b.count == 0, "row removed");

    budget_list_free(&b);
}

static void test_todo(void) {
    printf("[todo]\n");
    TodoList list;
    todo_list_init(&list);

    Todo t;
    todo_init(&t);
    t.id = 7;
    strcpy(t.title, "交房租");
    t.repeat = TODO_REPEAT_MONTHLY;
    t.repeat_day = 31;
    t.priority = TODO_PRIO_BOTH;
    todo_list_add(&list, &t);

    CHECK(todo_list_find(&list, 7)->repeat == TODO_REPEAT_MONTHLY, "repeat flag kept");
    CHECK(todo_list_find(&list, 7)->repeat_day == 31, "repeat_day (几号) kept");
    CHECK(todo_list_find(&list, 7)->priority == TODO_PRIO_BOTH, "priority kept");

    /* 四象限映射：象限号 0..3 ↔ priority 3..0 */
    CHECK(TODO_PRIO_QUAD(TODO_PRIO_BOTH) == 0, "重要且紧急 -> quad 0");
    CHECK(TODO_PRIO_QUAD(TODO_PRIO_IMPORTANT) == 1, "重要不紧急 -> quad 1");
    CHECK(TODO_PRIO_QUAD(TODO_PRIO_URGENT) == 2, "紧急不重要 -> quad 2");
    CHECK(TODO_PRIO_QUAD(TODO_PRIO_NONE) == 3, "不重要不紧急 -> quad 3");
    for (int q = 0; q < TODO_QUAD_COUNT; q++)
        CHECK(TODO_PRIO_QUAD(TODO_QUAD_PRIO(q)) == q, "quad <-> prio round trip");

    /* 新待办默认是「不重要」、没指定日子 */
    Todo dflt;
    todo_init(&dflt);
    CHECK(dflt.priority == TODO_PRIO_NONE, "new todo defaults to 不重要");
    CHECK(dflt.repeat_day == 0, "new todo has no repeat day");

    todo_list_soft_delete(&list, 7);
    CHECK(todo_list_find(&list, 7)->deleted == 1, "todo soft deleted");
    todo_list_restore(&list, 7);
    CHECK(todo_list_find(&list, 7)->deleted == 0, "todo restored");

    todo_list_free(&list);
}

static void test_storage(void) {
    printf("[storage]\n");
    char dir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (n == 0) { g_fail++; printf("  FAIL  GetTempPath\n"); return; }

    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%stodofile_selftest.json", dir);

    AccountList out;
    acct_list_init(&out);

    AccountRecord r;
    acct_init(&r);
    r.id = 42;
    r.amount = 12.34;
    strcpy(r.record_type, "income");
    strcpy(r.category, "工资,奖金");     /* 故意带逗号，下面验 CSV 转义 */
    strcpy(r.date, "2026-10-24");
    strcpy(r.note, "带\"引号\"的备注");
    acct_list_add(&out, &r);

    CHECK(storage_save_accounts(path, &out) == 1, "save accounts ok");

    AccountList in;
    acct_list_init(&in);
    CHECK(storage_load_accounts(path, &in) == 1, "load accounts ok");
    CHECK(in.count == 1, "one record round-tripped");
    if (in.count == 1) {
        CHECK(in.items[0].id == 42, "id survives round trip");
        CHECK(acct_is_income(&in.items[0]), "record_type survives round trip");
        CHECK(strcmp(in.items[0].category, "工资,奖金") == 0, "utf8 category survives");
    }
    acct_list_free(&in);

    /* 文件存在但内容坏掉 → 必须返回 -1，而不是当成空列表 */
    FILE* f = fopen(path, "wb");
    if (f) { fputs("{ this is not json", f); fclose(f); }
    AccountList bad;
    acct_list_init(&bad);
    CHECK(storage_load_accounts(path, &bad) == -1, "corrupted file returns -1");
    CHECK(bad.count == 0, "nothing loaded from corrupted file");
    acct_list_free(&bad);

    DeleteFileA(path);
    CHECK(storage_load_accounts(path, &bad) == 0, "missing file returns 0");

    /* CSV：带逗号/引号的字段要能被正确转义 */
    char csv[MAX_PATH * 2];
    snprintf(csv, sizeof(csv), "%saccounts_selftest.csv", dir);
    int wrote = storage_export_accounts_csv(csv, &out, 0);
    CHECK(wrote == 1, "csv export wrote 1 row");

    FILE* cf = fopen(csv, "rb");
    if (cf) {
        char buf[1024];
        size_t got = fread(buf, 1, sizeof(buf) - 1, cf);
        buf[got] = 0;
        fclose(cf);
        CHECK((unsigned char)buf[0] == 0xEF, "csv starts with UTF-8 BOM");
        CHECK(strstr(buf, "\"工资,奖金\"") != NULL, "comma field is quoted");
        CHECK(strstr(buf, "\"带\"\"引号\"\"的备注\"") != NULL, "inner quotes doubled");
        CHECK(strstr(buf, "收入") != NULL, "income rendered as 收入");
    } else {
        g_fail++;
        printf("  FAIL  csv file missing\n");
    }
    DeleteFileA(csv);

    /* 待办的新字段（优先级 / 每周周几 / 每月几号）必须能落盘再读回来 */
    {
        char tpath[MAX_PATH * 2];
        snprintf(tpath, sizeof(tpath), "%stodofile_self.json", dir);
        TodoList tl;
        todo_list_init(&tl);
        Todo td;
        todo_init(&td);
        td.id = 301;
        strcpy(td.title, "周会");
        td.repeat = TODO_REPEAT_WEEKLY;
        td.repeat_day = 3;                       /* 周三 */
        td.priority = TODO_PRIO_IMPORTANT;
        todo_list_add(&tl, &td);

        CHECK(storage_save_todos(tpath, &tl) == 1, "save todos ok");
        TodoList tl2;
        todo_list_init(&tl2);
        CHECK(storage_load_todos(tpath, &tl2) == 1, "load todos ok");
        if (tl2.count == 1) {
            CHECK(tl2.items[0].repeat_day == 3, "weekly day survives round trip");
            CHECK(tl2.items[0].priority == TODO_PRIO_IMPORTANT, "priority survives round trip");
        } else {
            g_fail++;
            printf("  FAIL  todo round trip count\n");
        }
        todo_list_free(&tl);
        todo_list_free(&tl2);
        DeleteFileA(tpath);
    }

    /* 按天备份：第一次建、第二次跳过（同一天只留一份） */
    {
        char bdir[MAX_PATH * 2];
        snprintf(bdir, sizeof(bdir), "%stodo_bak_selftest", dir);
        CreateDirectoryA(bdir, NULL);
        char src[MAX_PATH * 2];
        snprintf(src, sizeof(src), "%sdata_selftest.json", dir);
        FILE* sf = fopen(src, "wb");
        if (sf) { fputs("[{\"id\":1}]", sf); fclose(sf); }

        char pat[MAX_PATH * 4];
        snprintf(pat, sizeof(pat), "%s\\data_selftest-*.json", bdir);
        wipe_matching(pat);          /* 上次跑留下的备份先清掉，用例才可重复 */

        int first = storage_daily_backup(src, bdir, 30);
        CHECK(first == 1, "daily backup created once");
        int again = storage_daily_backup(src, bdir, 30);
        CHECK(again == 0, "second call same day is a no-op");

        /* 备份目录里应该恰好一个 <stem>-YYYYMMDD.json */
        WIN32_FIND_DATAA fd;
        HANDLE fh = FindFirstFileA(pat, &fd);
        CHECK(fh != INVALID_HANDLE_VALUE, "backup file matches <stem>-YYYYMMDD.json");
        if (fh != INVALID_HANDLE_VALUE) FindClose(fh);

        DeleteFileA(src);
        wipe_matching(pat);
    }

    acct_list_free(&out);
}

int main(void) {
    printf("TodoApp selftest\n-----------------\n");
    test_date();
    test_month_len();
    test_account();
    test_budget();
    test_todo();
    test_storage();
    printf("-----------------\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
