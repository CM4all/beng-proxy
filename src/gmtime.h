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

#define LIBCORE__STDCALL(x) x

LIBCORE__STDCALL(xbrokentime *)
sysx_time_gmtime(time_t tm32, xbrokentime *tmrec);

#endif
