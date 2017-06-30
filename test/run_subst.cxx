#include "StdioSink.hxx"
#include "istream/istream_subst.hxx"
#include "istream/istream_file.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "util/PrintException.hxx"

#include "util/Compiler.h"

int
main(int argc, char **argv)
try {
    Istream *istream;
    int i;

    const ScopeFbPoolInit fb_pool_init;
    PInstance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test", 8192);

    istream = istream_subst_new(pool,
                                *istream_file_new(instance.event_loop, *pool,
                                                  "/dev/stdin", (off_t)-1));

    for (i = 1; i <= argc - 2; i += 2) {
        istream_subst_add(*istream, argv[i], argv[i + 1]);
    }

    if (i < argc) {
        fprintf(stderr, "usage: %s [A1 B1 A2 B2 ...]\n", argv[0]);
        return 1;
    }

    StdioSink sink(*istream);

    pool_unref(pool);
    pool_commit();

    sink.LoopRead();
} catch (...) {
    PrintException(std::current_exception());
    return EXIT_FAILURE;
}
