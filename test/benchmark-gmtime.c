/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "time/gmtime.h"
#include "libcore-gmtime.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

int main(int argc, char **argv) {
    time_t now = time(NULL);
    int i;
    unsigned foo = 0;

    if (argc != 2)
        abort();

    if (strcmp(argv[1], "libc") == 0) {
        struct tm tm;
        for (i = 10000000; i > 0; --i) {
            gmtime_r(&now, &tm);
            foo += tm.tm_sec + tm.tm_year + tm.tm_mday + tm.tm_mon + tm.tm_hour + tm.tm_min;
        }
    } else if (strcmp(argv[1], "babak") == 0) {
        struct tm tm;
        xtime xnow = (xtime)now * 1000;
        for (i = 10000000; i > 0; --i) {
            sysx_time_gmtime_orig(xnow + i, &tm);
            foo += tm.tm_sec + tm.tm_year + tm.tm_mday + tm.tm_mon + tm.tm_hour + tm.tm_min;
        }
    } else if (strcmp(argv[1], "beng") == 0) {
        struct tm tm;
        for (i = 10000000; i > 0; --i) {
            sysx_time_gmtime(now + i, &tm);
            foo += tm.tm_sec + tm.tm_year + tm.tm_mday + tm.tm_mon + tm.tm_hour + tm.tm_min;
        }
    } else
        abort();

    printf("%u\n", foo);
}
