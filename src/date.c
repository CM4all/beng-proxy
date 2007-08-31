/*
 * Format and parse HTTP dates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "date.h"
#include "compiler.h"

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

static attr_always_inline const char *
wday_name(int wday)
{
    return wdays[likely(wday >= 0 && wday < 7)
                 ? wday : 7];
}

static attr_always_inline const char *
month_name(int month)
{
    return months[likely(month >= 0 && month < 12)
                  ? month : 12];
}

static attr_always_inline void
format_2digit(char *dest, unsigned number)
{
    dest[0] = '0' + number / 10;
    dest[1] = '0' + number % 10;
}

static attr_always_inline void
format_4digit(char *dest, unsigned number)
{
    dest[0] = '0' + number / 1000;
    dest[1] = '0' + (number / 100) % 10;
    dest[2] = '0' + (number / 10) % 10;
    dest[3] = '0' + number % 10;
}

void
http_date_format_r(char *buffer, time_t t)
{
    const struct tm *tm = gmtime(&t);

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
