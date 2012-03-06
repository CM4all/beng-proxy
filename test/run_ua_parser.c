#include "ua_classification.h"

#include <stdlib.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s PATH\n", argv[0]);
        return EXIT_FAILURE;
    }

    GError *error = NULL;
    if (!ua_classification_init(argv[1], &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    for (int i = 2; i < argc; ++i) {
        const char *ua = argv[i];
        const char *class = ua_classification_lookup(ua);
        printf("'%s' -> %s\n", ua, class);
    }

    ua_classification_deinit();
    return EXIT_SUCCESS;
}
