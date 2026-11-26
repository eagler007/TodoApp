#ifndef UTIL_H
#define UTIL_H

#include <windows.h>
#include <stdint.h>
#include <time.h>

#define DATE_FORMAT "%Y/%m/%d"
#define DATE_STR_LEN 16

void get_today(char* buf, int bufsize);
void get_now_str(char* buf, int bufsize);
void get_weekday_str(char* buf, int bufsize);
int parse_date(const char* date_str, struct tm* tm_out);
void format_date(const struct tm* tm_in, char* buf, int bufsize);
void get_month_from_date(const char* date_str, char* buf, int bufsize);
int is_valid_date(const char* date_str);
/* 某年某月有多少天。month: 1..12 */
int Util_DaysInMonth(int year, int month);
void utf8_to_wchar(const char* utf8, wchar_t* wide, int wide_len);
void wchar_to_utf8(const wchar_t* wide, char* utf8, int utf8_len);
char* strdup_a(const char* src);
void normalize_date(char* date);
/* 金额格式化：¥1,427.13 / -¥320.00；dec 只能是 0 或 2 */
void fmt_money_utf8(char* out, int cap, double v, int dec);

#endif
