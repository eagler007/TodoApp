#include "weather.h"
#include "config.h"
#include "util.h"

#include <wininet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * 国内：t.weather.itboy.net（中国天气网数据，免 key、纯 HTTP、国内直连快）
 *   data.wendu                  "19.3"        → 当前温度（四舍五入）
 *   data.shidu                  "77%"         → 湿度
 *   data.quality                "良"          → 空气质量等级
 *   data.forecast[0].type       "多云"        → 天气现象
 *   data.forecast[0].high       "高温 22℃"    → 今日最高
 *   data.forecast[0].low        "低温 18℃"    → 今日最低
 *
 * 国外：api.open-meteo.com（免 key、免注册、全球经纬度，HTTPS）
 *   current.temperature_2m             10.7   → 当前温度
 *   current.relative_humidity_2m       65     → 湿度
 *   current.weather_code               0      → WMO 代码，翻成中文
 *   daily.temperature_2m_max[0]        13.4   → 今日最高
 *   daily.temperature_2m_min[0]        9.1    → 今日最低
 *   air-quality-api 的 current.european_aqi → 空气质量
 *
 * 两个源都不引 JSON 库：只按 "key": 硬抠几个字段。
 */

#define WC_CN(n, c)          { n, c, 0.0f, 0.0f, WC_R_CN }
#define WC_W(n, la, lo, r)   { n, "", (float)(la), (float)(lo), r }

const char* const kWeatherRegions[WC_R_COUNT] = {
    "国内", "亚洲", "欧洲", "北美", "南美", "大洋洲", "非洲"
};

const WeatherCity kWeatherCities[] = {
    /* ---- 国内（中国天气网城市码） ---- */
    WC_CN("上海",     "101020100"),
    WC_CN("北京",     "101010100"),
    WC_CN("天津",     "101030100"),
    WC_CN("重庆",     "101040100"),
    WC_CN("广州",     "101280101"),
    WC_CN("深圳",     "101280601"),
    WC_CN("杭州",     "101210101"),
    WC_CN("南京",     "101190101"),
    WC_CN("苏州",     "101190401"),
    WC_CN("无锡",     "101190201"),
    WC_CN("常州",     "101191101"),
    WC_CN("南通",     "101190501"),
    WC_CN("宁波",     "101210401"),
    WC_CN("温州",     "101210701"),
    WC_CN("合肥",     "101220101"),
    WC_CN("福州",     "101230101"),
    WC_CN("厦门",     "101230201"),
    WC_CN("南昌",     "101240101"),
    WC_CN("济南",     "101120101"),
    WC_CN("青岛",     "101120201"),
    WC_CN("郑州",     "101180101"),
    WC_CN("武汉",     "101200101"),
    WC_CN("长沙",     "101250101"),
    WC_CN("成都",     "101270101"),
    WC_CN("西安",     "101110101"),
    WC_CN("沈阳",     "101070101"),
    WC_CN("大连",     "101070201"),
    WC_CN("长春",     "101060101"),
    WC_CN("哈尔滨",   "101050101"),
    WC_CN("石家庄",   "101090101"),
    WC_CN("太原",     "101100101"),
    WC_CN("呼和浩特", "101080101"),
    WC_CN("兰州",     "101160101"),
    WC_CN("西宁",     "101150101"),
    WC_CN("银川",     "101170101"),
    WC_CN("乌鲁木齐", "101130101"),
    WC_CN("拉萨",     "101140101"),
    WC_CN("昆明",     "101290101"),
    WC_CN("贵阳",     "101260101"),
    WC_CN("南宁",     "101300101"),
    WC_CN("海口",     "101310101"),
    WC_CN("东莞",     "101281601"),
    WC_CN("佛山",     "101280800"),
    WC_CN("香港",     "101320101"),
    WC_CN("澳门",     "101330101"),
    WC_CN("台北",     "101340101"),

    /* ---- 亚洲 ---- */
    WC_W("东京",       35.6895, 139.6917, WC_R_ASIA),
    WC_W("大阪",       34.6937, 135.5023, WC_R_ASIA),
    WC_W("札幌",       43.0621, 141.3544, WC_R_ASIA),
    WC_W("首尔",       37.5665, 126.9780, WC_R_ASIA),
    WC_W("釜山",       35.1796, 129.0756, WC_R_ASIA),
    WC_W("新加坡",      1.3521, 103.8198, WC_R_ASIA),
    WC_W("曼谷",       13.7563, 100.5018, WC_R_ASIA),
    WC_W("清迈",       18.7883,  98.9853, WC_R_ASIA),
    WC_W("吉隆坡",      3.1390, 101.6869, WC_R_ASIA),
    WC_W("雅加达",     -6.2088, 106.8456, WC_R_ASIA),
    WC_W("巴厘岛",     -8.4095, 115.1889, WC_R_ASIA),
    WC_W("马尼拉",     14.5995, 120.9842, WC_R_ASIA),
    WC_W("河内",       21.0278, 105.8342, WC_R_ASIA),
    WC_W("胡志明市",   10.8231, 106.6297, WC_R_ASIA),
    WC_W("新德里",     28.6139,  77.2090, WC_R_ASIA),
    WC_W("孟买",       19.0760,  72.8777, WC_R_ASIA),
    WC_W("迪拜",       25.2048,  55.2708, WC_R_ASIA),
    WC_W("伊斯坦布尔", 41.0082,  28.9784, WC_R_ASIA),

    /* ---- 欧洲 ---- */
    WC_W("伦敦",       51.5072,  -0.1276, WC_R_EUROPE),
    WC_W("巴黎",       48.8566,   2.3522, WC_R_EUROPE),
    WC_W("柏林",       52.5200,  13.4050, WC_R_EUROPE),
    WC_W("慕尼黑",     48.1351,  11.5820, WC_R_EUROPE),
    WC_W("法兰克福",   50.1109,   8.6821, WC_R_EUROPE),
    WC_W("阿姆斯特丹", 52.3676,   4.9041, WC_R_EUROPE),
    WC_W("布鲁塞尔",   50.8503,   4.3517, WC_R_EUROPE),
    WC_W("苏黎世",     47.3769,   8.5417, WC_R_EUROPE),
    WC_W("日内瓦",     46.2044,   6.1432, WC_R_EUROPE),
    WC_W("罗马",       41.9028,  12.4964, WC_R_EUROPE),
    WC_W("米兰",       45.4642,   9.1900, WC_R_EUROPE),
    WC_W("马德里",     40.4168,  -3.7038, WC_R_EUROPE),
    WC_W("巴塞罗那",   41.3874,   2.1686, WC_R_EUROPE),
    WC_W("里斯本",     38.7223,  -9.1393, WC_R_EUROPE),
    WC_W("维也纳",     48.2082,  16.3738, WC_R_EUROPE),
    WC_W("布拉格",     50.0755,  14.4378, WC_R_EUROPE),
    WC_W("华沙",       52.2297,  21.0122, WC_R_EUROPE),
    WC_W("布达佩斯",   47.4979,  19.0402, WC_R_EUROPE),
    WC_W("斯德哥尔摩", 59.3293,  18.0686, WC_R_EUROPE),
    WC_W("奥斯陆",     59.9139,  10.7522, WC_R_EUROPE),
    WC_W("哥本哈根",   55.6761,  12.5683, WC_R_EUROPE),
    WC_W("赫尔辛基",   60.1699,  24.9384, WC_R_EUROPE),
    WC_W("都柏林",     53.3498,  -6.2603, WC_R_EUROPE),
    WC_W("雅典",       37.9838,  23.7275, WC_R_EUROPE),
    WC_W("莫斯科",     55.7558,  37.6173, WC_R_EUROPE),

    /* ---- 北美 ---- */
    WC_W("纽约",       40.7128, -74.0060, WC_R_NAMERICA),
    WC_W("洛杉矶",     34.0522,-118.2437, WC_R_NAMERICA),
    WC_W("旧金山",     37.7749,-122.4194, WC_R_NAMERICA),
    WC_W("西雅图",     47.6062,-122.3321, WC_R_NAMERICA),
    WC_W("芝加哥",     41.8781, -87.6298, WC_R_NAMERICA),
    WC_W("波士顿",     42.3601, -71.0589, WC_R_NAMERICA),
    WC_W("华盛顿",     38.9072, -77.0369, WC_R_NAMERICA),
    WC_W("拉斯维加斯", 36.1699,-115.1398, WC_R_NAMERICA),
    WC_W("迈阿密",     25.7617, -80.1918, WC_R_NAMERICA),
    WC_W("休斯敦",     29.7604, -95.3698, WC_R_NAMERICA),
    WC_W("多伦多",     43.6532, -79.3832, WC_R_NAMERICA),
    WC_W("温哥华",     49.2827,-123.1207, WC_R_NAMERICA),
    WC_W("蒙特利尔",   45.5017, -73.5673, WC_R_NAMERICA),
    WC_W("墨西哥城",   19.4326, -99.1332, WC_R_NAMERICA),

    /* ---- 南美 ---- */
    WC_W("圣保罗",    -23.5505, -46.6333, WC_R_SAMERICA),
    WC_W("里约热内卢",-22.9068, -43.1729, WC_R_SAMERICA),
    WC_W("布宜诺斯艾利斯", -34.6037, -58.3816, WC_R_SAMERICA),
    WC_W("圣地亚哥",  -33.4489, -70.6693, WC_R_SAMERICA),
    WC_W("利马",      -12.0464, -77.0428, WC_R_SAMERICA),
    WC_W("波哥大",      4.7110, -74.0721, WC_R_SAMERICA),

    /* ---- 大洋洲 ---- */
    WC_W("悉尼",      -33.8688, 151.2093, WC_R_OCEANIA),
    WC_W("墨尔本",    -37.8136, 144.9631, WC_R_OCEANIA),
    WC_W("布里斯班",  -27.4698, 153.0251, WC_R_OCEANIA),
    WC_W("珀斯",      -31.9505, 115.8605, WC_R_OCEANIA),
    WC_W("奥克兰",    -36.8485, 174.7633, WC_R_OCEANIA),
    WC_W("惠灵顿",    -41.2865, 174.7762, WC_R_OCEANIA),

    /* ---- 非洲 ---- */
    WC_W("开罗",       30.0444,  31.2357, WC_R_AFRICA),
    WC_W("约翰内斯堡",-26.2041,  28.0473, WC_R_AFRICA),
    WC_W("开普敦",    -33.9249,  18.4241, WC_R_AFRICA),
    WC_W("内罗毕",     -1.2921,  36.8219, WC_R_AFRICA),
    WC_W("拉各斯",      6.5244,   3.3792, WC_R_AFRICA),
    WC_W("卡萨布兰卡", 33.5731,  -7.5898, WC_R_AFRICA),
};
const int kWeatherCityCount = (int)(sizeof(kWeatherCities) / sizeof(kWeatherCities[0]));

#define REFRESH_MS (30 * 60 * 1000)   /* 每 30 分钟刷一次 */

static HWND   g_notify;
static UINT   g_msg;
static HANDLE g_thread;
static HANDLE g_wake;
static volatile LONG g_quit;
static CRITICAL_SECTION g_cs;
static int    g_inited;
static HINTERNET g_inet;                 /* 共用句柄：连接池 / TLS 会话靠它复用 */
static LONG   g_seq;                     /* 换城市序号，用来丢弃过期的异步结果 */

static char g_city[64]  = "上海";
static char g_code[16]  = "101020100";   /* 国际城市为空串 */
static float g_lat, g_lon;
static int  g_intl;                      /* 1 = 走 Open-Meteo */
static char g_text[96];                  /* "上海 多云 19°C" */
static char g_brief[96];                 /* "上海 19°C"（窗口窄时用） */
static char g_summary[128];              /* "多云 18~22°C · 湿度 77% · 空气质量 良" */
static int  g_ok;                        /* 至少成功拉到过一次 */

/* UTF-8 的 "°"：不能直接写字面量度符号，源码编码一变就烂 */
#define DEG "\xC2\xB0"

/* 城市名 → 内部状态（code 为空即国际城市）。调用方要持有 g_cs */
static void apply_city_locked(const char* name) {
    int i = -1;
    if (name && name[0]) {
        for (int k = 0; k < kWeatherCityCount; k++)
            if (strcmp(kWeatherCities[k].name, name) == 0) { i = k; break; }
    }
    if (i < 0) {
        strcpy(g_city, "上海");
        strcpy(g_code, "101020100");
        g_lat = g_lon = 0.0f;
        g_intl = 0;
        return;
    }
    strncpy(g_city, kWeatherCities[i].name, sizeof(g_city) - 1);
    g_city[sizeof(g_city) - 1] = 0;
    strncpy(g_code, kWeatherCities[i].code, sizeof(g_code) - 1);
    g_code[sizeof(g_code) - 1] = 0;
    g_lat  = kWeatherCities[i].lat;
    g_lon  = kWeatherCities[i].lon;
    g_intl = kWeatherCities[i].code[0] ? 0 : 1;
}

/*
 * 共用 g_inet 句柄，不再每次请求都 InternetOpen —— 那个动作会重新 DNS + TLS 握手，
 * air-quality-api 冷启动实测要 12 秒（暖机后 1.3 秒），换个句柄复用能省掉大半。
 * 超时给 6 秒：主天气接口 1.6 秒内返回，AQI 冷启动可能超，超了就当没有。
 */
static int http_get(const WCHAR* url, char* out, int cap) {
    if (!out || cap <= 2) return 0;
    out[0] = 0;
    if (!g_inet) return 0;

    DWORD to = 6000;
    InternetSetOptionW(g_inet, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof(to));
    InternetSetOptionW(g_inet, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));
    InternetSetOptionW(g_inet, INTERNET_OPTION_SEND_TIMEOUT,    &to, sizeof(to));

    int ok = 0;
    HINTERNET req = InternetOpenUrlW(g_inet, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_NO_UI    | INTERNET_FLAG_NO_COOKIES, 0);
    if (req) {
        int n = 0;
        while (n < cap - 1) {
            DWORD got = 0;
            if (!InternetReadFile(req, out + n, (DWORD)(cap - 1 - n), &got) || got == 0)
                break;
            n += (int)got;
        }
        out[n] = 0;
        ok = (n > 0);
        InternetCloseHandle(req);
    }
    return ok;
}

/* 从 JSON 里抠出 "wendu":"19.3" 的整数部分（四舍五入；冬天会是 "-10.2"） */
static int parse_wendu(const char* json, int* out) {
    if (!json || !out) return 0;
    const char* p = strstr(json, "\"wendu\":\"");
    if (!p) return 0;
    p += 9;
    if (*p != '-' && (*p < '0' || *p > '9')) return 0;   /* 空串 / 非数字：当作没拿到 */
    double v = atof(p);
    *out = (int)(v >= 0 ? v + 0.5 : v - 0.5);
    return 1;
}

/* 取 "key":"值" 里的值（UTF-8），start 为搜索起点 */
static int grab_str(const char* start, const char* key, char* out, int cap) {
    if (!start || !key || !out || cap <= 1) return 0;
    out[0] = 0;
    char pat[48];
    _snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    pat[sizeof(pat) - 1] = 0;
    const char* p = strstr(start, pat);
    if (!p) return 0;
    p += strlen(pat);
    const char* e = strchr(p, '"');
    if (!e || e == p) return 0;
    int n = (int)(e - p);
    if (n > cap - 1) n = cap - 1;
    memcpy(out, p, (size_t)n);
    out[n] = 0;
    return n > 0;
}

/* 取 "high":"高温 22℃" 里的 22（"℃" 是 UTF-8 三字节，不影响：只读数字） */
static int grab_num(const char* start, const char* key, int* out) {
    char buf[48];
    if (!grab_str(start, key, buf, sizeof(buf))) return 0;
    const char* p = buf;
    while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
    if (!*p) return 0;
    *out = atoi(p);
    return 1;
}

/* 取 "key":10.7 或 "key":[13.4] 里的数字（Open-Meteo 的值不带引号） */
static int grab_flt(const char* start, const char* key, double* out) {
    if (!start || !key || !out) return 0;
    char pat[48];
    _snprintf(pat, sizeof(pat), "\"%s\":", key);
    pat[sizeof(pat) - 1] = 0;
    const char* p = strstr(start, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '[') p++;      /* daily 里的值是数组 */
    char* end = NULL;
    double v = strtod(p, &end);
    if (end == p) return 0;
    *out = v;
    return 1;
}

/* WMO 天气代码 → 中文（Open-Meteo 用这套，0–99，见 WMO 4677） */
static const char* wmo_text(int c) {
    switch (c) {
    case 0:  return "晴";
    case 1:  return "晴间多云";
    case 2:  return "多云";
    case 3:  return "阴";
    case 45: case 48: return "雾";
    case 51: case 53: case 55: return "毛毛雨";
    case 56: case 57: return "冻毛毛雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 66: case 67: return "冻雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "米雪";
    case 80: case 81: return "阵雨";
    case 82: return "强阵雨";
    case 85: case 86: return "阵雪";
    case 95: return "雷阵雨";
    case 96: case 99: return "雷阵雨伴冰雹";
    default: return "";                      /* 认不出就不显示，别瞎猜 */
    }
}

/* 欧洲 AQI 分档 → 国内熟悉的那四个等级（不追求严格换算，看得懂就行） */
static const char* aqi_text(double aqi) {
    if (aqi < 0)    return "";
    if (aqi <= 20)  return "优";
    if (aqi <= 40)  return "良";
    if (aqi <= 60)  return "中";
    if (aqi <= 80)  return "差";
    if (aqi <= 100) return "很差";
    return "极差";
}

/* 一次取数的结果 */
typedef struct {
    int  temp;
    int  got;
    char type[24];
    char shidu[16];
    char quality[16];
    int  high, low, has_hl;
} Reading;

/* 国内：中国天气网 */
static int fetch_cn(const char* code, Reading* r) {
    WCHAR wcode[16], url[256];
    utf8_to_wchar(code, wcode, 16);
    _snwprintf(url, 256, L"http://t.weather.itboy.net/api/weather/city/%s", wcode);
    url[255] = 0;

    char body[8192];
    if (!http_get(url, body, sizeof(body))) return 0;

    int t = 0;
    if (!parse_wendu(body, &t)) return 0;
    r->temp = t;
    r->got  = 1;

    /* 气象字段在 forecast[0] 里，先定位数组，避免抓到 yesterday 的旧数据 */
    const char* fc = strstr(body, "\"forecast\":[");
    if (!fc) fc = body;
    grab_str(fc, "type", r->type, sizeof(r->type));
    r->has_hl = (grab_num(fc, "high", &r->high) & grab_num(fc, "low", &r->low));
    grab_str(body, "shidu", r->shidu, sizeof(r->shidu));
    grab_str(body, "quality", r->quality, sizeof(r->quality));
    return 1;
}

/* 国外：Open-Meteo。两请求：天气 + 空气质量（AQI 拿不到不影响主数据） */
static int fetch_intl(float lat, float lon, Reading* r) {
    WCHAR url[512];

    {
        char u8[400];
        _snprintf(u8, sizeof(u8),
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                  "&current=temperature_2m,relative_humidity_2m,weather_code"
                  "&daily=temperature_2m_max,temperature_2m_min"
                  "&timezone=auto&forecast_days=1",
                  (double)lat, (double)lon);
        u8[sizeof(u8) - 1] = 0;
        utf8_to_wchar(u8, url, 512);
    }

    char body[4096];
    if (!http_get(url, body, sizeof(body))) return 0;

    const char* cur = strstr(body, "\"current\":{");
    if (!cur) cur = body;
    const char* day = strstr(body, "\"daily\":{");
    if (!day) day = body;

    double tv = 0, hv = 0;
    if (!grab_flt(cur, "temperature_2m", &tv)) return 0;
    r->temp = (int)(tv >= 0 ? tv + 0.5 : tv - 0.5);
    r->got  = 1;

    double code = 0;
    if (grab_flt(cur, "weather_code", &code)) {
        const char* w = wmo_text((int)code);
        strncpy(r->type, w, sizeof(r->type) - 1);
        r->type[sizeof(r->type) - 1] = 0;
    }
    if (grab_flt(cur, "relative_humidity_2m", &hv))
        _snprintf(r->shidu, sizeof(r->shidu), "%d%%", (int)(hv + 0.5));

    int h1 = grab_flt(day, "temperature_2m_max", &tv);
    if (h1) r->high = (int)(tv >= 0 ? tv + 0.5 : tv - 0.5);
    int h2 = grab_flt(day, "temperature_2m_min", &tv);
    if (h2) r->low  = (int)(tv >= 0 ? tv + 0.5 : tv - 0.5);
    r->has_hl = (h1 && h2);
    return 1;
}

/*
 * 欧洲 AQI 分档。单独一个请求、单独一次调用：它所在的域名冷启动能要 12 秒，
 * 绝不能挡在温度前面 —— 温度先显示，AQI 拿到再补（拿不到就算）。
 */
static const char* fetch_aqi(float lat, float lon, char* out, int cap) {
    out[0] = 0;
    char u8[400];
    _snprintf(u8, sizeof(u8),
              "https://air-quality-api.open-meteo.com/v1/air-quality"
              "?latitude=%.4f&longitude=%.4f&current=european_aqi",
              (double)lat, (double)lon);
    u8[sizeof(u8) - 1] = 0;
    WCHAR wu[512];
    utf8_to_wchar(u8, wu, 512);

    char ab[2048];
    if (!http_get(wu, ab, sizeof(ab))) return out;
    /* ⚠️ "european_aqi" 在 current_units 里也出现一次（值是 "EAQI" 字符串），
       不先定位 "current":{ 就会抠到那个引号串，strtod 失败当没拿到 */
    const char* cur = strstr(ab, "\"current\":{");
    if (!cur) cur = ab;
    double aqi = -1;
    if (!grab_flt(cur, "european_aqi", &aqi)) return out;
    strncpy(out, aqi_text(aqi), (size_t)cap - 1);
    out[cap - 1] = 0;
    return out;
}

static DWORD WINAPI weather_thread(LPVOID arg) {
    (void)arg;
    while (!g_quit) {
        char  code[16];
        char  city[64];
        float lat, lon;
        int   intl;
        LONG  seq;

        EnterCriticalSection(&g_cs);
        strncpy(code, g_code, sizeof(code) - 1);
        code[sizeof(code) - 1] = 0;
        strncpy(city, g_city, sizeof(city) - 1);
        city[sizeof(city) - 1] = 0;
        lat  = g_lat;
        lon  = g_lon;
        intl = g_intl;
        seq  = g_seq;
        LeaveCriticalSection(&g_cs);

        Reading rd;
        memset(&rd, 0, sizeof(rd));
        if (intl) fetch_intl(lat, lon, &rd);
        else      fetch_cn(code, &rd);
        if (rd.got && !g_quit) {
            EnterCriticalSection(&g_cs);
            char num[24];
            _snprintf(num, sizeof(num), "%d" DEG "C", rd.temp);
            num[sizeof(num) - 1] = 0;

            _snprintf(g_brief, sizeof(g_brief), "%s %s", city, num);
            g_brief[sizeof(g_brief) - 1] = 0;

            if (rd.type[0])
                _snprintf(g_text, sizeof(g_text), "%s %s %s", city, rd.type, num);
            else
                _snprintf(g_text, sizeof(g_text), "%s", g_brief);
            g_text[sizeof(g_text) - 1] = 0;

            /* 摘要逐段拼：缺哪段就少哪段，不要因为没空气质量把湿度也丢掉 */
            if (rd.type[0] || rd.has_hl || rd.shidu[0] || rd.quality[0]) {
                char seg[128];
                g_summary[0] = 0;

                seg[0] = 0;
                if (rd.type[0] && rd.has_hl)
                    _snprintf(seg, sizeof(seg), "%s %d~%d" DEG "C", rd.type, rd.low, rd.high);
                else if (rd.type[0])
                    _snprintf(seg, sizeof(seg), "%s", rd.type);
                else if (rd.has_hl)
                    _snprintf(seg, sizeof(seg), "%d~%d" DEG "C", rd.low, rd.high);
                seg[sizeof(seg) - 1] = 0;
                strcpy(g_summary, seg);

                if (rd.shidu[0]) {
                    _snprintf(seg, sizeof(seg), " · 湿度 %s", rd.shidu);
                    seg[sizeof(seg) - 1] = 0;
                    strncat(g_summary, seg, sizeof(g_summary) - strlen(g_summary) - 1);
                }
                if (rd.quality[0]) {
                    _snprintf(seg, sizeof(seg), " · 空气质量 %s", rd.quality);
                    seg[sizeof(seg) - 1] = 0;
                    strncat(g_summary, seg, sizeof(g_summary) - strlen(g_summary) - 1);
                }
                g_summary[sizeof(g_summary) - 1] = 0;
            } else {
                g_summary[0] = 0;
            }
            g_ok = 1;
            LeaveCriticalSection(&g_cs);
        }
        if (g_notify) PostMessageW(g_notify, g_msg, rd.got ? 1 : 0, 0);

        /* 国际城市的空气质量：单独补一次。慢（冷启动 12 秒）也不挡温度，
           拿到再往摘要后面接一段；期间换过城市（seq 变了）就丢掉 */
        if (rd.got && intl && !g_quit) {
            char q[16];
            if (fetch_aqi(lat, lon, q, sizeof(q)) && q[0]) {
                int updated = 0;
                EnterCriticalSection(&g_cs);
                if (g_seq == seq && g_summary[0]) {
                    char seg[64];
                    _snprintf(seg, sizeof(seg), " · 空气质量 %s", q);
                    seg[sizeof(seg) - 1] = 0;
                    strncat(g_summary, seg, sizeof(g_summary) - strlen(g_summary) - 1);
                    updated = 1;
                }
                LeaveCriticalSection(&g_cs);
                if (updated && g_notify) PostMessageW(g_notify, g_msg, 1, 0);
            }
        }

        /* 分片等待：换城市 / 退出都能立刻中断 */
        DWORD waited = 0;
        while (!g_quit && waited < REFRESH_MS) {
            if (WaitForSingleObject(g_wake, 1000) == WAIT_OBJECT_0) break;
            waited += 1000;
        }
    }
    return 0;
}

void Weather_Init(HWND notify, UINT msg_ready) {
    if (g_inited) return;
    InitializeCriticalSection(&g_cs);
    g_inited = 1;
    g_notify = notify;
    g_msg    = msg_ready;

    /* 只认 city 一个键：按名字查表补出 code / 经纬度 / 走哪个源。
       老配置里的 city_code 不再依赖（否则换了城市没换码就会串城市） */
    char v[64];
    if (Config_Get("city", v, sizeof(v)) && v[0]) {
        EnterCriticalSection(&g_cs);
        apply_city_locked(v);
        LeaveCriticalSection(&g_cs);
    }

    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_inet = InternetOpenW(L"TodoApp/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                           NULL, NULL, 0);
    /* 栈给 128 KB 就够（线程里只有几个 KB 的 URL / 接收缓冲），
       默认 1 MB 会让私有内存白白多出近 1 MB */
    g_thread = CreateThread(NULL, 128 * 1024, weather_thread, NULL, 0, NULL);
    if (g_thread) SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Weather_Shutdown(void) {
    if (!g_inited) return;
    InterlockedExchange(&g_quit, 1);
    if (g_wake) SetEvent(g_wake);
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_wake) { CloseHandle(g_wake); g_wake = NULL; }
    if (g_inet) { InternetCloseHandle(g_inet); g_inet = NULL; }
    DeleteCriticalSection(&g_cs);
    g_inited = 0;
}

void Weather_SetCity(const char* name) {
    if (!name || !name[0]) return;
    EnterCriticalSection(&g_cs);
    apply_city_locked(name);
    g_text[0] = 0;          /* 换城市先把旧温度清掉，别显示错城市的温度 */
    g_brief[0] = 0;
    g_summary[0] = 0;
    g_ok = 0;
    g_seq++;                /* 作废正在飞的 AQI 结果 */
    char city[64], code[16];
    strcpy(city, g_city);
    strcpy(code, g_code);
    LeaveCriticalSection(&g_cs);

    Config_Set("city", city);
    /* 国际城市没有城市码：写空串等于删掉这个键 */
    Config_Set("city_code", code);

    /* 立刻通知主线程重绘一次：城市名要马上变，不能等网络回来才刷 */
    if (g_notify) PostMessageW(g_notify, g_msg, 0, 0);
    if (g_wake) SetEvent(g_wake);   /* 立刻重拉，不用等 30 分钟 */
}

const char* Weather_City(void) { return g_city; }
const char* Weather_Code(void) { return g_code; }

int Weather_Text(char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!g_inited) return 0;
    EnterCriticalSection(&g_cs);
    int n = 0;
    if (g_ok) {
        strncpy(out, g_text, (size_t)cap - 1);
        out[cap - 1] = 0;
        n = (int)strlen(out);
    }
    LeaveCriticalSection(&g_cs);
    return n;
}

int Weather_Brief(char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!g_inited) return 0;
    EnterCriticalSection(&g_cs);
    int n = 0;
    if (g_ok) {
        strncpy(out, g_brief, (size_t)cap - 1);
        out[cap - 1] = 0;
        n = (int)strlen(out);
    }
    LeaveCriticalSection(&g_cs);
    return n;
}

int Weather_Summary(char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!g_inited) return 0;
    EnterCriticalSection(&g_cs);
    int n = 0;
    if (g_ok) {
        strncpy(out, g_summary, (size_t)cap - 1);
        out[cap - 1] = 0;
        n = (int)strlen(out);
    }
    LeaveCriticalSection(&g_cs);
    return n;
}

int Weather_FindCity(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < kWeatherCityCount; i++)
        if (strcmp(kWeatherCities[i].name, name) == 0) return i;
    return -1;
}

int Weather_RegionOf(const char* name) {
    int i = Weather_FindCity(name);
    return (i >= 0) ? (int)kWeatherCities[i].region : WC_R_CN;
}
