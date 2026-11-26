#ifndef WEATHER_H
#define WEATHER_H

#include <windows.h>

/*
 * 标题栏里的「城市 + 气象 + 气温」。
 *
 * 网络请求放在后台线程里跑（WinINet，连接/接收各 4 秒超时），拿到结果后
 * PostMessage 通知主窗口重绘 —— 断网 / 接口挂了都只是不显示，绝不拖慢启动。
 * 每 30 分钟自动刷新一次；换城市会立刻重拉。
 *
 * 两个数据源（城市表里按 code 是否为空自动选）：
 *   国内 —— t.weather.itboy.net（中国天气网数据，中文气象名更贴切）
 *   国外 —— api.open-meteo.com（免费、免 key、全球经纬度，WMO 代码翻中文）
 */

/* 地区分组：设置城市对话框里先用它筛一遍，避免一百多个城市拉一个大列表 */
#define WC_R_CN       0
#define WC_R_ASIA     1
#define WC_R_EUROPE   2
#define WC_R_NAMERICA 3
#define WC_R_SAMERICA 4
#define WC_R_OCEANIA  5
#define WC_R_AFRICA   6
#define WC_R_COUNT    7

typedef struct {
    const char*   name;   /* UTF-8 城市名 */
    /* 中国天气网城市码。国际城市为 ""，此时改走 lat/lon */
    const char*   code;
    float         lat;    /* 纬度（负数为南纬）。国内城市填 0，不使用 */
    float         lon;    /* 经度（负数为西经） */
    unsigned char region; /* WC_R_* */
} WeatherCity;

extern const WeatherCity   kWeatherCities[];
extern const int           kWeatherCityCount;
extern const char* const   kWeatherRegions[WC_R_COUNT];

/* 必须在 Config_Init 之后调用。notify 收到的消息：wParam = 1 成功 / 0 失败 */
void Weather_Init(HWND notify, UINT msg_ready);
void Weather_Shutdown(void);

/* 换城市：按城市名查表决定走哪个源，写 config.ini 并立刻重新拉取 */
void Weather_SetCity(const char* name);

const char* Weather_City(void);          /* UTF-8，失败时也有值 */
const char* Weather_Code(void);          /* 国际城市返回 "" */

/* 返回 "上海 多云 19°C"（UTF-8，带天气现象）。还没拿到数据时返回 0，out 写空串 */
int Weather_Text(char* out, int cap);

/* 只返回 "上海 19°C"（不带气象），窗口窄时退化用。没数据返回 0 */
int Weather_Brief(char* out, int cap);

/* 当日概况："多云 18~22°C · 湿度 77% · 空气质量 良"；没数据返回 0 */
int Weather_Summary(char* out, int cap);

/* 按城市名找表里的下标，找不到返回 -1 */
int Weather_FindCity(const char* name);

/* 按城市名取地区（WC_R_*），认不出返回 WC_R_CN */
int Weather_RegionOf(const char* name);

#endif
