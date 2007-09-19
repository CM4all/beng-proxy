#include "gmtime.h"
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
            sysx_time_gmtime_orig(xnow, &tm);
            foo += tm.tm_sec + tm.tm_year + tm.tm_mday + tm.tm_mon + tm.tm_hour + tm.tm_min;
        }
    } else if (strcmp(argv[1], "beng") == 0) {
        struct tm tm;
        for (i = 10000000; i > 0; --i) {
            sysx_time_gmtime(now, &tm);
            foo += tm.tm_sec + tm.tm_year + tm.tm_mday + tm.tm_mon + tm.tm_hour + tm.tm_min;
        }
    } else
        abort();

    printf("%u\n", foo);
}
