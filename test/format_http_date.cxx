#include "http_date.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int main(int argc, char **argv) {
    time_t t;

    if (argc >= 2)
        t = strtoul(argv[1], nullptr, 10);
    else
        t = time(nullptr);

    printf("%s\n", http_date_format(t));
}
