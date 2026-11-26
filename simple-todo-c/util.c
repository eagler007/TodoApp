#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void get_today(char* buf, int bufsize) {
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    strftime(buf, bufsize, DATE_FORMAT, tm_now);
}

void get_now_str(char* buf, int bufsize) {
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    strftime(buf, bufsize, "%Y-%m-%dT%H:%M:%S", tm_now);
}

void get_weekday_str(char* buf, int bufsize) {
    static const char* days[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    int wday = tm_now->tm_wday;
    if (wday == 0) wday = 6;
    else wday -= 1;
    strncpy(buf, days[wday], bufsize - 1);
    buf[bufsize - 1] = '\0';
}

int parse_date(const char* date_str, struct tm* tm_out) {
    if (!date_str || strlen(date_str) < 10) return 0;
    memset(tm_out, 0, sizeof(struct tm));
    for (int i = 0; i < 2; i++) {
        char sep = (i == 0) ? '/' : '-';
        if (date_str[4] == sep && date_str[7] == sep) {
            tm_out->tm_year = atoi(date_str) - 1900;
            tm_out->tm_mon = atoi(date_str + 5) - 1;
            tm_out->tm_mday = atoi(date_str + 8);
            return 1;
        }
    }
    return 0;
}

void format_date(const struct tm* tm_in, char* buf, int bufsize) {
    strftime(buf, bufsize, DATE_FORMAT, tm_in);
}

void get_month_from_date(const char* date_str, char* buf, int bufsize) {
    const char* sep_list[] = {"/", "-"};
    for (int i = 0; i < 2; i++) {
        const char* sep = sep_list[i];
        const char* p = strstr(date_str, sep);
        if (p) {
            int year_len = (int)(p - date_str);
            if (year_len > 0 && year_len < bufsize) {
                strncpy(buf, date_str, year_len);
                buf[year_len] = '\0';
                const char* mon_start = p + strlen(sep);
                char mon[4] = {0};
                int j = 0;
                while (mon_start[j] && mon_start[j] != '/' && mon_start[j] != '-' && j < 2) {
                    mon[j] = mon_start[j];
                    j++;
                }
                mon[j] = '\0';
                if (strlen(mon) < 2) {
                    char padded[4];
                    snprintf(padded, sizeof(padded), "%02d", atoi(mon));
                    strcpy(mon, padded);
                }
                /* 拼到临时缓冲再拷回，别用 snprintf(buf, ..., buf, ...) 自重叠 */
                char joined[40];
                snprintf(joined, sizeof(joined), "%s-%s", buf, mon);
                strncpy(buf, joined, (size_t)bufsize - 1);
                buf[bufsize - 1] = '\0';
                return;
            }
        }
    }
    buf[0] = '\0';
}

int is_valid_date(const char* date_str) {
    if (!date_str || strlen(date_str) != 10) return 0;
    if (date_str[4] != '/' || date_str[7] != '/') return 0;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (date_str[i] < '0' || date_str[i] > '9') return 0;
    }
    int year = atoi(date_str);
    int month = atoi(date_str + 5);
    int day = atoi(date_str + 8);
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > 31) return 0;
    int max_day = Util_DaysInMonth(year, month);
    return day <= max_day;
}

int Util_DaysInMonth(int year, int month) {
    static const int d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        return 29;
    return d[month - 1];
}

void utf8_to_wchar(const char* utf8, wchar_t* wide, int wide_len) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_len);
}

void wchar_to_utf8(const wchar_t* wide, char* utf8, int utf8_len) {
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8_len, NULL, NULL);
}

void normalize_date(char* date) {
    for (char* p = date; *p; p++) {
        if (*p == '-') *p = '/';
    }
}

char* strdup_a(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char* dst = (char*)malloc(len);
    if (dst) memcpy(dst, src, len);
    return dst;
}

/* 金额格式化：整数部分补千分位，前面带 ¥，负数把负号放到 ¥ 之前 */
void fmt_money_utf8(char* out, int cap, double v, int dec) {
    if (!out || cap <= 0) return;
    int neg = (v < 0);
    double a = neg ? -v : v;
    char raw[48];
    if (dec == 2) snprintf(raw, sizeof(raw), "%.2f", a);
    else          snprintf(raw, sizeof(raw), "%.0f", a);

    char ip[32], frac[8];
    frac[0] = 0;
    char* dot = strchr(raw, '.');
    if (dot) {
        size_t k = (size_t)(dot - raw);
        if (k > 31) k = 31;
        memcpy(ip, raw, k);
        ip[k] = 0;
        strncpy(frac, dot, sizeof(frac) - 1);
        frac[sizeof(frac) - 1] = 0;
    } else {
        strncpy(ip, raw, sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = 0;
    }

    char grp[48];
    int j = 0, len = (int)strlen(ip);
    for (int i = 0; i < len && j < 40; i++) {
        if (i > 0 && (len - i) % 3 == 0) grp[j++] = ',';
        grp[j++] = ip[i];
    }
    grp[j] = 0;

    snprintf(out, (size_t)cap, "%s%s%s%s", neg ? "-" : "", "\xC2\xA5", grp, frac);
}
