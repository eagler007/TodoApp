#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

/*
 * config.ini 的通用键值读写（UTF-8，一行一条 `key=value`）。
 *
 * 以前只有 theme.c 自己 fopen/覆盖写，谁再往里加一项就会被冲掉；
 * 统一走这里之后，theme、天气城市可以共存。
 */

/* config_path 为 NULL 时只把内部路径清空（测试用） */
void Config_Init(const WCHAR* config_path);
const WCHAR* Config_Path(void);

/* 返回 1 表示找到了这个键 */
int  Config_Get(const char* key, char* out, int cap);

/* 写入/更新一个键；文件里其它行原样保留。value 传 NULL 或空串表示删除该键 */
void Config_Set(const char* key, const char* value);

#endif
