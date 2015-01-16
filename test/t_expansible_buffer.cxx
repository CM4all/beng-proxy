#include "expansible_buffer.hxx"
#include "pool.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    struct pool *pool;
    struct expansible_buffer *eb;
    const void *p, *q;
    size_t size;

    pool = pool_new_libc(nullptr, "root");

    eb = expansible_buffer_new(pool, 4, 1024);
    assert(expansible_buffer_is_empty(eb));

    p = expansible_buffer_read(eb, &size);
    assert(p != nullptr);
    assert(size == 0);

    expansible_buffer_write_string(eb, "01");
    assert(!expansible_buffer_is_empty(eb));

    q = expansible_buffer_read(eb, &size);
    assert(q == p);
    assert(size == 2);
    assert(memcmp(q, "01", 2) == 0);

    expansible_buffer_write_string(eb, "234");
    assert(!expansible_buffer_is_empty(eb));

    q = expansible_buffer_read(eb, &size);
    assert(q != p);
    assert(size == 5);
    assert(memcmp(q, "01234", 5) == 0);

    expansible_buffer_reset(eb);
    assert(expansible_buffer_is_empty(eb));

    p = expansible_buffer_read(eb, &size);
    assert(p == q);
    assert(size == 0);

    expansible_buffer_write_string(eb, "abcdef");
    assert(!expansible_buffer_is_empty(eb));

    p = expansible_buffer_read(eb, &size);
    assert(p == q);
    assert(size == 6);
    assert(memcmp(q, "abcdef", 6) == 0);

    p = expansible_buffer_write(eb, 512);
    assert(p != nullptr);

    /* this call hits the hard limit */
    p = expansible_buffer_write(eb, 512);
    assert(p == nullptr);

    pool_commit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
