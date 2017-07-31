#include "expansible_buffer.hxx"
#include "TestPool.hxx"
#include "util/ConstBuffer.hxx"

#include "util/Compiler.h"

#include <assert.h>
#include <string.h>

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    TestPool pool;

    ExpansibleBuffer eb(pool, 4, 1024);
    assert(eb.IsEmpty());

    auto p = eb.Read();
    assert(p.data != nullptr);
    assert(p.size == 0);

    eb.Write("01");
    assert(!eb.IsEmpty());

    auto q = eb.Read();
    assert(q.data == p.data);
    assert(q.size == 2);
    assert(memcmp(q.data, "01", 2) == 0);

    eb.Write("234");
    assert(!eb.IsEmpty());

    q = eb.Read();
    assert(q.data != p.data);
    assert(q.size == 5);
    assert(memcmp(q.data, "01234", 5) == 0);

    eb.Clear();
    assert(eb.IsEmpty());

    p = eb.Read();
    assert(p.data == q.data);
    assert(p.size == 0);

    eb.Write("abcdef");
    assert(!eb.IsEmpty());

    p = eb.Read();
    assert(p.data == q.data);
    assert(p.size == 6);
    assert(memcmp(q.data, "abcdef", 6) == 0);

    void *r = eb.Write(512);
    assert(r != nullptr);

    /* this call hits the hard limit */
    r = eb.Write(512);
    assert(r == nullptr);
}
