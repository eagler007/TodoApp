#ifndef MENU_H
#define MENU_H

#include <windows.h>

typedef struct {
    int          id;      /* 0 且 label 为 NULL 时表示分隔线 */
    const WCHAR* label;
    /* 左侧色点：0 = 不画，>0 = 分类色号 + 1（Ui_CatChipColors / Ui_CatPresetIndex 那套）。
       老代码用 {id, label} 两项初始化，第三字段默认 0 正好是「不画」，不会误标。 */
    int          dot;
} UiMenuItem;

/*
 * 自绘弹出菜单（替代 TrackPopupMenu）。
 * owner : 所属窗口
 * sx,sy : 屏幕坐标（左上角）
 * 返回选中项 id，取消返回 0。
 */
int Ui_PopMenu(HWND owner, int sx, int sy, const UiMenuItem* items, int n);

/* 按控件矩形弹出（左对齐其左下角） */
int Ui_PopMenuAt(HWND owner, const RECT* anchor_client, const UiMenuItem* items, int n);

/*
 * 下拉列表专用：指定宽度 + 当前项打标记。
 * wfix > 0 用固定宽度（下拉框要和输入框等宽），wfix == 0 自动测宽。
 * cur_id != 0 时该 id 的项用强调色并在左侧画一个圆点。
 * 同一时刻只允许存在一个弹出层，嵌套调用直接返回 0（避免点击一次叠出好几层窗）。
 */
int Ui_PopMenuSel(HWND owner, int sx, int sy, int wfix,
                  const UiMenuItem* items, int n, int cur_id);

/* 同上，但可以指定列数（0 = 自动：超过 12 项排 3 列）。
 * 「每月几号」31 项如果竖排会有一千多像素，比屏幕还高。 */
int Ui_PopMenuGrid(HWND owner, int sx, int sy, int wfix,
                   const UiMenuItem* items, int n, int cur_id, int cols);

/* ---------------- 自绘下拉框 ---------------- */
/* 要装得下「每月 1-31 号」，还要装得下国内城市表（46 个） */
#define UI_COMBO_MAX 64

HWND Ui_ComboCreate(HWND parent, int id, HINSTANCE hInst,
                    const char* const* utf8_items, int n);
void Ui_ComboAddItem(HWND h, const char* utf8_item);          /* 追加一项（去重） */
void Ui_ComboClear(HWND h);                                   /* 清空所有项，sel = -1 */
void Ui_ComboSetSel(HWND h, int idx);
/* 按文本选中（找不到返回 0，不改变当前选择） */
int  Ui_ComboSetText(HWND h, const char* utf8_item);
int  Ui_ComboGetSel(HWND h);                                   /* -1 = 空 */
int  Ui_ComboGetText(HWND h, char* out, int cap);              /* UTF-8，返回 1 表示有值 */
/*
 * 打开「按分类上色」：每一项按文本去匹配 kStdCategories 的色号，
 * 选中值和弹出项都会在左侧画一个该分类的色点、文字也用同色系。
 * 只对认得的分类名生效（日期、周几之类的项自动无色点），所以开着也没副作用。
 */
void Ui_ComboSetColorize(HWND h, int on);
/* 给某一项显式指定色号（-1 = 不画点）。会自动打开 colorize。
 * 优先级下拉框这种「名字对不上分类表」的用法靠它上色。 */
void Ui_ComboSetItemDot(HWND h, int idx, int color_idx);

#endif
