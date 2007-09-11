#include "gmtime.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
    time_t now = time(NULL);
    int i;

    if (argc != 2)
        abort();

    if (strcmp(argv[1], "libc") == 0) {
        for (i = 10000000; i > 0; --i)
            gmtime(&now);
    } else if (strcmp(argv[1], "babak") == 0) {
        struct tm tm;
        for (i = 10000000; i > 0; --i)
            sysx_time_gmtime(now, &tm);
    } else
        abort();
}
