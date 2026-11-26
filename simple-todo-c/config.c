#include "config.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define CFG_MAX_LINES 64
#define CFG_LINE_LEN  256

static WCHAR g_path[MAX_PATH];
static WCHAR g_tmp[MAX_PATH];

/* 一次读全文件到内存，改完整体写回。行数很少（个位数），不值得做流式编辑 */
static char g_lines[CFG_MAX_LINES][CFG_LINE_LEN];
static int  g_n;

void Config_Init(const WCHAR* config_path) {
    g_path[0] = 0;
    if (config_path && config_path[0]) {
        wcsncpy(g_path, config_path, MAX_PATH - 1);
        g_path[MAX_PATH - 1] = 0;
    }
}

const WCHAR* Config_Path(void) { return g_path; }

static void cfg_load(void) {
    g_n = 0;
    if (!g_path[0]) return;
    FILE* f = _wfopen(g_path, L"rt");
    if (!f) return;
    while (g_n < CFG_MAX_LINES && fgets(g_lines[g_n], CFG_LINE_LEN, f)) {
        /* 去掉行尾换行 */
        size_t k = strlen(g_lines[g_n]);
        while (k > 0 && (g_lines[g_n][k - 1] == '\n' || g_lines[g_n][k - 1] == '\r'))
            g_lines[g_n][--k] = 0;
        if (k > 0) g_n++;
    }
    fclose(f);
}

/* 判断某一行是不是 key=... */
static int line_is_key(const char* line, const char* key) {
    size_t kl = strlen(key);
    if (strncmp(line, key, kl) != 0) return 0;
    return (line[kl] == '=');
}

int Config_Get(const char* key, char* out, int cap) {
    if (out && cap > 0) out[0] = 0;
    if (!key || !key[0] || !out || cap <= 0) return 0;
    cfg_load();
    for (int i = 0; i < g_n; i++) {
        if (!line_is_key(g_lines[i], key)) continue;
        const char* v = strchr(g_lines[i], '=');
        if (!v) return 0;
        v++;
        strncpy(out, v, (size_t)cap - 1);
        out[cap - 1] = 0;
        return 1;
    }
    return 0;
}

void Config_Set(const char* key, const char* value) {
    if (!key || !key[0] || !g_path[0]) return;
    cfg_load();

    char want[CFG_LINE_LEN];
    int has_val = (value && value[0]);
    if (has_val) _snprintf(want, sizeof(want), "%s=%s", key, value);
    want[sizeof(want) - 1] = 0;

    /* 先找已有的同名键：有就原地替换（或删除），没有就追加到末尾 */
    int found = -1;
    for (int i = 0; i < g_n; i++) {
        if (line_is_key(g_lines[i], key)) { found = i; break; }
    }
    if (found >= 0) {
        if (has_val) {
            strncpy(g_lines[found], want, CFG_LINE_LEN - 1);
            g_lines[found][CFG_LINE_LEN - 1] = 0;
        } else {
            for (int i = found; i < g_n - 1; i++)
                memcpy(g_lines[i], g_lines[i + 1], CFG_LINE_LEN);
            g_n--;
        }
    } else if (has_val && g_n < CFG_MAX_LINES) {
        strncpy(g_lines[g_n], want, CFG_LINE_LEN - 1);
        g_lines[g_n][CFG_LINE_LEN - 1] = 0;
        g_n++;
    }

    /* 先写临时文件再替换：写一半断电也不会把配置写成空的 */
    _snwprintf(g_tmp, MAX_PATH, L"%s.tmp", g_path);
    g_tmp[MAX_PATH - 1] = 0;
    FILE* f = _wfopen(g_tmp, L"wt");
    if (!f) return;
    for (int i = 0; i < g_n; i++) fprintf(f, "%s\n", g_lines[i]);
    fclose(f);
    MoveFileExW(g_tmp, g_path, MOVEFILE_REPLACE_EXISTING);
}
