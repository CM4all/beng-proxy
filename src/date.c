/*
 * Format and parse HTTP dates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "date.h"
#include "gmtime.h"
#include "format.h"
#include "strutil.h"

#include <stdint.h>

static const char wdays[8][4] = {
    "Sun,",
    "Mon,",
    "Tue,",
    "Wed,",
    "Thu,",
    "Fri,",
    "Sat,",
    "???,",
};

static const char months[13][4] = {
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

static __attr_always_inline const char *
wday_name(int wday)
{
    return wdays[likely(wday >= 0 && wday < 7)
                 ? wday : 7];
}

static __attr_always_inline const char *
month_name(int month)
{
    return months[likely(month >= 0 && month < 12)
                  ? month : 12];
}

void
http_date_format_r(char *buffer, time_t t)
{
    static struct tm tm_buffer;
    const struct tm *tm = sysx_time_gmtime(t, &tm_buffer);

    *(uint32_t*)buffer = *(const uint32_t*)wday_name(tm->tm_wday);
    buffer[4] = ' ';
    format_2digit(buffer + 5, tm->tm_mday);
    buffer[7] = ' ';
    *(uint32_t*)(buffer + 8) = *(const uint32_t*)month_name(tm->tm_mon);
    format_4digit(buffer + 12, tm->tm_year + 1900);
    buffer[16] = ' ';
    format_2digit(buffer + 17, tm->tm_hour);
    buffer[19] = ':';
    format_2digit(buffer + 20, tm->tm_min);
    buffer[22] = ':';
    format_2digit(buffer + 23, tm->tm_sec);
    buffer[25] = ' ';
    *(uint32_t*)(buffer + 26) = *(const uint32_t*)"GMT";
}

static char buffer[30];

const char *
http_date_format(time_t t)
{
    http_date_format_r(buffer, t);
    return buffer;
}

static int
parse_2digit(const char *p)
{
    if (!char_is_digit(p[0]) || !char_is_digit(p[1]))
        return -1;

    return (p[0] - '0') * 10 + (p[1] - '0');
}

static int
parse_4digit(const char *p)
{
    if (!char_is_digit(p[0]) || !char_is_digit(p[1]) ||
        !char_is_digit(p[2]) || !char_is_digit(p[3]))
        return -1;

    return (p[0] - '0') * 1000 + (p[1] - '0') * 100
        + (p[2] - '0') * 10 + (p[3] - '0');
}

static int
parse_month_name(const char *p)
{
    int i;

    for (i = 0; i < 12; ++i)
        if (*(const uint32_t*)months[i] == *(const uint32_t*)p)
            return i;

    return -1;
}

time_t
http_date_parse(const char *p)
{
    struct tm tm;

    if (strlen(p) < 25)
        return (time_t)-1;

    tm.tm_sec = parse_2digit(p + 23);
    tm.tm_min = parse_2digit(p + 20);
    tm.tm_hour = parse_2digit(p + 17);
    tm.tm_mday = parse_2digit(p + 5);
    tm.tm_mon = parse_month_name(p + 8);
    tm.tm_year = parse_4digit(p + 12);

    if (tm.tm_sec == -1 || tm.tm_min == -1 || tm.tm_hour == -1 ||
        tm.tm_mday == -1 || tm.tm_mon == -1 || tm.tm_year < 1900)
        return (time_t)-1;

    tm.tm_year -= 1900;
    tm.tm_isdst = -1;

    return timegm(&tm);
}
