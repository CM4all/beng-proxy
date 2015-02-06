#include "escape_html.hxx"
#include "escape_static.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2)
        exit(1);

    const char *p = argv[1];
    const char *q = unescape_static(&html_escape_class, p, strlen(p));
    if (q == NULL) {
        fprintf(stderr, "too long\n");
        return EXIT_FAILURE;
    }

    printf("%s\n", q);
}
