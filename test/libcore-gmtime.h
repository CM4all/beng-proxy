/*
 * For comparing the original libcore gmtime() implementation with my
 * optimized one, this file provides libcore's unmodified version.
 */

#include "gmtime.h"

typedef int64_t				xtime;

LIBCORE__STDCALL(xbrokentime *)
sysx_time_gmtime_orig(xtime tm64, xbrokentime *tmrec);
