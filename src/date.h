/*
 * Format and parse HTTP dates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DATE_H
#define __BENG_DATE_H

#include <time.h>

void
http_date_format_r(char *buffer, time_t t);

const char *
http_date_format(time_t t);

#endif
