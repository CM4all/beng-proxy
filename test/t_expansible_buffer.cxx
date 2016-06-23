#include "expansible_buffer.hxx"
#include "RootPool.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    const void *p, *q;
    size_t size;

    RootPool pool;

    ExpansibleBuffer eb(pool, 4, 1024);
    assert(eb.IsEmpty());

    p = eb.Read(&size);
    assert(p != nullptr);
    assert(size == 0);

    eb.Write("01");
    assert(!eb.IsEmpty());

    q = eb.Read(&size);
    assert(q == p);
    assert(size == 2);
    assert(memcmp(q, "01", 2) == 0);

    eb.Write("234");
    assert(!eb.IsEmpty());

    q = eb.Read(&size);
    assert(q != p);
    assert(size == 5);
    assert(memcmp(q, "01234", 5) == 0);

    eb.Clear();
    assert(eb.IsEmpty());

    p = eb.Read(&size);
    assert(p == q);
    assert(size == 0);

    eb.Write("abcdef");
    assert(!eb.IsEmpty());

    p = eb.Read(&size);
    assert(p == q);
    assert(size == 6);
    assert(memcmp(q, "abcdef", 6) == 0);

    p = eb.Write(512);
    assert(p != nullptr);

    /* this call hits the hard limit */
    p = eb.Write(512);
    assert(p == nullptr);
}
