#include "theme.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static Theme       g_theme;

#define C(r, g, b) RGB(r, g, b)

static void apply_light(Theme* t) {
    memset(t, 0, sizeof(*t));
    t->dark = 0;

    t->bg          = C(0xF3, 0xF3, 0xF3);
    t->surface     = C(0xFF, 0xFF, 0xFF);
    t->surface_alt = C(0xFA, 0xFA, 0xFA);
    t->border      = C(0xE2, 0xE2, 0xE2);
    t->border_soft = C(0xEF, 0xEF, 0xEF);

    t->text        = C(0x1B, 0x1B, 0x1B);
    t->text_sub    = C(0x5C, 0x5C, 0x5C);
    t->text_dim    = C(0x8A, 0x8A, 0x8A);

    t->accent        = C(0x0F, 0x6C, 0xBD);
    t->accent_hover  = C(0x1A, 0x86, 0xD9);
    t->accent_down   = C(0x0B, 0x5A, 0xA1);
    t->accent_fg     = C(0xFF, 0xFF, 0xFF);
    t->accent_soft   = C(0xEA, 0xF3, 0xFB);
    t->accent_soft_fg= C(0x0F, 0x6C, 0xBD);

    t->selection   = C(0xE5, 0xF1, 0xFB);
    t->sum_a       = C(0x0F, 0x6C, 0xBD);
    t->sum_b       = C(0x1A, 0x86, 0xD9);
    t->sum_fg      = C(0xFF, 0xFF, 0xFF);
    t->sum_sub     = C(0xCF, 0xE4, 0xF8);

    t->input_bg    = C(0xFF, 0xFF, 0xFF);
    t->input_border= C(0xBF, 0xBF, 0xBF);
    t->list_bg     = C(0xFF, 0xFF, 0xFF);
    t->hover_bg    = C(0xEB, 0xEB, 0xEB);
    t->danger      = C(0xC4, 0x2B, 0x1C);
    t->success     = C(0x10, 0x7C, 0x10);
    t->warning     = C(0xB2, 0x5A, 0x00);   /* 待办「重要」：深橙，白底上够对比 */
}

static void apply_dark(Theme* t) {
    memset(t, 0, sizeof(*t));
    t->dark = 1;

    t->bg          = C(0x14, 0x15, 0x18);
    t->surface     = C(0x1E, 0x20, 0x24);
    t->surface_alt = C(0x26, 0x2A, 0x30);
    t->border      = C(0x2F, 0x32, 0x37);
    t->border_soft = C(0x26, 0x2A, 0x2E);

    t->text        = C(0xE9, 0xE9, 0xE9);
    t->text_sub    = C(0x9B, 0xA1, 0xAA);
    t->text_dim    = C(0x6E, 0x74, 0x7D);

    t->accent        = C(0xE0, 0xA8, 0x4E);
    t->accent_hover  = C(0xED, 0xBB, 0x6A);
    t->accent_down   = C(0xC9, 0x90, 0x3C);
    t->accent_fg     = C(0x1A, 0x15, 0x09);
    t->accent_soft   = C(0x2E, 0x2A, 0x20);
    t->accent_soft_fg= C(0xE0, 0xA8, 0x4E);

    t->selection   = C(0x33, 0x30, 0x1F);
    t->sum_a       = C(0x1E, 0x20, 0x24);
    t->sum_b       = C(0x26, 0x2A, 0x2E);
    t->sum_fg      = C(0xE0, 0xA8, 0x4E);
    t->sum_sub     = C(0x7C, 0x81, 0x88);

    t->input_bg    = C(0x1E, 0x20, 0x24);
    t->input_border= C(0x4A, 0x4F, 0x57);
    t->list_bg     = C(0x1E, 0x20, 0x24);
    t->hover_bg    = C(0x2C, 0x2F, 0x34);
    t->danger      = C(0xE5, 0x53, 0x4B);
    t->success     = C(0x4C, 0xC3, 0x57);
    t->warning     = C(0xE0, 0xA8, 0x4E);   /* 深色底上换成琥珀金 */
}

static void load_config(void) {
    char v[32];
    if (Config_Get("theme", v, sizeof(v))) {
        if (strcmp(v, "dark") == 0) apply_dark(&g_theme);
        else                        apply_light(&g_theme);
    }
}

void Theme_Init(const WCHAR* config_path) {
    apply_light(&g_theme);
    /* 配置读写统一走 config.c：theme 和天气城市写的是同一个 config.ini，
       谁都不能整文件覆盖，否则另一项会被冲掉 */
    Config_Init(config_path);
    load_config();
}

const Theme* Theme_Now(void) { return &g_theme; }
int Theme_IsDark(void) { return g_theme.dark; }

void Theme_SetDark(int dark) {
    if (dark == g_theme.dark) return;
    if (dark) apply_dark(&g_theme); else apply_light(&g_theme);
}

void Theme_Toggle(void) {
    Theme_SetDark(!g_theme.dark);
    Theme_Save();
}

void Theme_Save(void) {
    Config_Set("theme", g_theme.dark ? "dark" : "light");
}
