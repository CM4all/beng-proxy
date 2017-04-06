/*
 * Format and parse HTTP dates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_date.hxx"
#include "util/CharUtil.hxx"
#include "util/DecimalFormat.h"

extern "C" {
#include "gmtime.h"
}

#include <stdint.h>
#include <stdbool.h>

static constexpr char wdays[8][5] = {
    "Sun,",
    "Mon,",
    "Tue,",
    "Wed,",
    "Thu,",
    "Fri,",
    "Sat,",
    "???,",
};

static constexpr char months[13][5] = {
    "Jan ",
    "Feb ",
    "Mar ",
    "Apr ",
    "May ",
    "Jun ",
    "Jul ",
    "Aug ",
    "Sep ",
    "Oct ",
    "Nov ",
    "Dec ",
    "???,",
};

static gcc_always_inline const char *
wday_name(int wday)
{
    return wdays[likely(wday >= 0 && wday < 7)
                 ? wday : 7];
}

static gcc_always_inline const char *
month_name(int month)
{
    return months[likely(month >= 0 && month < 12)
                  ? month : 12];
}

void
http_date_format_r(char *buffer, std::chrono::system_clock::time_point t)
{
    static struct tm tm_buffer;
    const struct tm *tm = sysx_time_gmtime(std::chrono::system_clock::to_time_t(t), &tm_buffer);

    *(uint32_t *)(void *)buffer = *(const uint32_t *)(const void *)wday_name(tm->tm_wday);
    buffer[4] = ' ';
    format_2digit(buffer + 5, tm->tm_mday);
    buffer[7] = ' ';
    *(uint32_t *)(void *)(buffer + 8) = *(const uint32_t *)(const void *)month_name(tm->tm_mon);
    format_4digit(buffer + 12, tm->tm_year + 1900);
    buffer[16] = ' ';
    format_2digit(buffer + 17, tm->tm_hour);
    buffer[19] = ':';
    format_2digit(buffer + 20, tm->tm_min);
    buffer[22] = ':';
    format_2digit(buffer + 23, tm->tm_sec);
    buffer[25] = ' ';
    *(uint32_t *)(void *)(buffer + 26) = *(const uint32_t *)(const void *)"GMT";
}

static char buffer[30];

const char *
http_date_format(std::chrono::system_clock::time_point t)
{
    http_date_format_r(buffer, t);
    return buffer;
}

static int
parse_2digit(const char *p)
{
    if (!IsDigitASCII(p[0]) || !IsDigitASCII(p[1]))
        return -1;

    return (p[0] - '0') * 10 + (p[1] - '0');
}

static int
parse_4digit(const char *p)
{
    if (!IsDigitASCII(p[0]) || !IsDigitASCII(p[1]) ||
        !IsDigitASCII(p[2]) || !IsDigitASCII(p[3]))
        return -1;

    return (p[0] - '0') * 1000 + (p[1] - '0') * 100
        + (p[2] - '0') * 10 + (p[3] - '0');
}

#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

static int
parse_month_name(const char *p)
{
    int i;

    for (i = 0; i < 12; ++i)
        if (*(const uint32_t *)(const void *)months[i] == *(const uint32_t *)(const void *)p)
            return i;

    return -1;
}

#if GCC_CHECK_VERSION(4,6)
#pragma GCC diagnostic pop
#endif

std::chrono::system_clock::time_point
http_date_parse(const char *p)
{
    struct tm tm;

    if (strlen(p) < 25)
        return std::chrono::system_clock::from_time_t(-1);

    tm.tm_sec = parse_2digit(p + 23);
    tm.tm_min = parse_2digit(p + 20);
    tm.tm_hour = parse_2digit(p + 17);
    tm.tm_mday = parse_2digit(p + 5);
    tm.tm_mon = parse_month_name(p + 8);
    tm.tm_year = parse_4digit(p + 12);

    if (tm.tm_sec == -1 || tm.tm_min == -1 || tm.tm_hour == -1 ||
        tm.tm_mday == -1 || tm.tm_mon == -1 || tm.tm_year < 1900)
        return std::chrono::system_clock::from_time_t(-1);

    tm.tm_year -= 1900;
    tm.tm_isdst = -1;

    return std::chrono::system_clock::from_time_t(timegm(&tm));
}
