#include "html-escape.h"
#include "strref.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    struct strref s;
    size_t used;

    if (argc != 2)
        exit(1);

    s.length = strlen(argv[1]);
    s.data = argv[1];

    used = html_escape(&s);
    if (used == 0) {
        printf("unchanged\n");
    } else if (used == strlen(argv[1])) {
        printf("ok\n");
        printf("%.*s\n", (int)s.length, s.data);
    } else {
        printf("partial %zu\n", used);
        printf("%.*s\n", (int)s.length, s.data);
    }
}
