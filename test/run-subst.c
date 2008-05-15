#include "istream.h"
#include "compiler.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static bool should_exit;

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    ssize_t nbytes;

    (void)ctx;

    nbytes = write(1, data, length);
    if (nbytes < 0) {
        fprintf(stderr, "failed to write to stdout: %s\n",
                strerror(errno));
        exit(2);
    }

    if (nbytes == 0) {
        fprintf(stderr, "failed to write to stdout\n");
        exit(2);
    }

    return (size_t)nbytes;
}

static void
my_istream_eof(void *ctx)
{
    (void)ctx;
    should_exit = true;
}

static void attr_noreturn
my_istream_abort(void *ctx)
{
    (void)ctx;
    exit(2);
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * main
 *
 */

int main(int argc, char **argv) {
    pool_t root_pool, pool;
    istream_t istream;
    int i;

    root_pool = pool_new_libc(NULL, "root");

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = istream_subst_new(pool, istream_file_new(pool, "/dev/stdin", (off_t)-1));

    for (i = 1; i <= argc - 2; i += 2) {
        istream_subst_add(istream, argv[i], argv[i + 1]);
    }

    if (i < argc) {
        fprintf(stderr, "usage: %s [A1 B1 A2 B2 ...]\n", argv[0]);
        return 1;
    }

    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    while (!should_exit)
        istream_read(istream);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
