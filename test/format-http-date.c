#include "date.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int main(int argc, char **argv) {
    time_t t;

    if (argc >= 2)
        t = strtoul(argv[1], NULL, 10);
    else
        t = time(NULL);

    printf("%s\n", http_date_format(t));
}
