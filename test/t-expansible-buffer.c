#include "expansible-buffer.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t pool;
    struct expansible_buffer *eb;
    const void *p, *q;
    size_t size;

    pool = pool_new_libc(NULL, "root");

    eb = expansible_buffer_new(pool, 4);
    assert(expansible_buffer_is_empty(eb));

    p = expansible_buffer_read(eb, &size);
    assert(p != NULL);
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

    pool_commit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
