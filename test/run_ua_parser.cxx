#include "ua_classification.hxx"
#include "util/Error.hxx"

#include <stdlib.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s PATH\n", argv[0]);
        return EXIT_FAILURE;
    }

    Error error;
    if (!ua_classification_init(argv[1], error)) {
        fprintf(stderr, "%s\n", error.GetMessage());
        return EXIT_FAILURE;
    }

    for (int i = 2; i < argc; ++i) {
        const char *ua = argv[i];
        const char *cls = ua_classification_lookup(ua);
        printf("'%s' -> %s\n", ua, cls);
    }

    ua_classification_deinit();
    return EXIT_SUCCESS;
}
