#include "startup.h"
#include "config.h"
#include <windows.h>

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VAL L"TodoApp"
#define CMD_CAP (MAX_PATH + 32)

/* 拼出要写进注册表的命令："<exe 全路径>" --tray
 * 路径必须带引号 —— 装在 Program Files 这类带空格的目录下，不带引号会被
 * 拆成两个参数，开机时静默失败（表现为"设了自启但从来没起来过"）。 */
static void build_cmd(WCHAR* out, int cap) {
    WCHAR exe[MAX_PATH];
    out[0] = 0;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    /* 纯手拼，不用 _snwprintf：GCC 16 运行时里 %s 对宽字符按窄串解析，容易踩 */
    if ((int)wcslen(exe) + 16 >= cap) return;
    wcscpy(out, L"\"");
    wcscat(out, exe);
    wcscat(out, L"\" --tray");
}

static int read_run(WCHAR* out, int cap) {
    HKEY k;
    DWORD type = 0, sz = (DWORD)(cap * (int)sizeof(WCHAR));
    out[0] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    int ok = (RegQueryValueExW(k, RUN_VAL, NULL, &type, (LPBYTE)out, &sz) == ERROR_SUCCESS)
             && (type == REG_SZ || type == REG_EXPAND_SZ);
    RegCloseKey(k);
    if (!ok) out[0] = 0;
    return ok && out[0];
}

static void write_reg(int on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (on) {
        WCHAR cmd[CMD_CAP];
        build_cmd(cmd, CMD_CAP);
        if (cmd[0])
            RegSetValueExW(k, RUN_VAL, 0, REG_SZ, (const BYTE*)cmd,
                           (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)));
    } else {
        /* 值本来就不存在时 RegDeleteValue 返回 ERROR_FILE_NOT_FOUND，无害 */
        RegDeleteValueW(k, RUN_VAL);
    }
    RegCloseKey(k);
}

int Startup_IsEnabled(void) {
    WCHAR want[CMD_CAP], have[CMD_CAP];
    build_cmd(want, CMD_CAP);
    if (!want[0]) return 0;
    if (!read_run(have, CMD_CAP)) return 0;
    return _wcsicmp(want, have) == 0;
}

void Startup_Apply(int on) {
    write_reg(on);
    Config_Set("autostart", on ? "1" : "0");
}

void Startup_Ensure(void) {
    char v[8];
    int want = 1;                       /* 缺省开启：这是提醒能不能收到的前提 */
    if (Config_Get("autostart", v, sizeof(v)) && v[0])
        want = (v[0] == '1');

    if (!want) {
        write_reg(0);                   /* 用户取消过 → 别偷偷加回来 */
        return;
    }
    /* 没注册、或注册的路径不是当前 exe（换过目录 / 还是老版本留下的死链）→ 重写 */
    if (!Startup_IsEnabled()) write_reg(1);
}
