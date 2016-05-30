#include "StdioSink.hxx"
#include "istream/istream_subst.hxx"
#include "istream/istream_file.hxx"
#include "event/Loop.hxx"
#include "fb_pool.hxx"
#include "RootPool.hxx"

#include <inline/compiler.h>

int main(int argc, char **argv) {
    struct pool *pool;
    Istream *istream;
    int i;

    EventLoop event_loop;
    fb_pool_init(event_loop, false);

    RootPool root_pool;

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = istream_subst_new(pool,
                                *istream_file_new(pool, "/dev/stdin", (off_t)-1,
                                                  nullptr));

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

    fb_pool_deinit();
}
