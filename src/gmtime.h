/*
 * Babak's fast gmtime() implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GMTIME_H
#define __BENG_GMTIME_H

#include <stdint.h>
#include <time.h>

typedef struct tm xbrokentime;
typedef int64_t xtime;

#define LIBCORE__STDCALL(x) x

LIBCORE__STDCALL(xbrokentime *)
sysx_time_gmtime(xtime tm64, xbrokentime *tmrec);

#endif
