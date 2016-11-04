#include "ua_classification.hxx"
#include "util/PrintException.hxx"

#include <stdlib.h>
#include <stdio.h>

int
main(int argc, char **argv)
try {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s PATH\n", argv[0]);
        return EXIT_FAILURE;
    }

    ua_classification_init(argv[1]);

    for (int i = 2; i < argc; ++i) {
        const char *ua = argv[i];
        const char *cls = ua_classification_lookup(ua);
        printf("'%s' -> %s\n", ua, cls);
    }

    ua_classification_deinit();
    return EXIT_SUCCESS;
} catch (...) {
    PrintException(std::current_exception());
    return EXIT_FAILURE;
}
