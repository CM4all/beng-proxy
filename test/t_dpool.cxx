#include "shm/shm.h"
#include "shm/dpool.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    void *a, *b, *c, *d;

    auto *shm = shm_new(1024, 2);
    assert(shm != nullptr);

    auto *pool = dpool_new(shm);
    assert(pool != nullptr);
    assert(!dpool_is_fragmented(pool));

    a = shm_alloc(shm, 1);
    assert(a != nullptr);
    assert(a != pool);

    b = shm_alloc(shm, 1);
    assert(b == nullptr);

    shm_free(shm, a);

    a = d_malloc(pool, 512);
    assert(a != nullptr);
    memset(a, 0, 512);
    assert(!dpool_is_fragmented(pool));

    b = d_malloc(pool, 800);
    assert(b != nullptr);
    memset(b, 0, 800);
    assert(!dpool_is_fragmented(pool));

    c = d_malloc(pool, 512);
    assert(c == nullptr);

    d = d_malloc(pool, 220);
    assert(d != nullptr);
    assert(!dpool_is_fragmented(pool));

    d_free(pool, a);
    assert(dpool_is_fragmented(pool));

    a = d_malloc(pool, 240);
    assert(a != nullptr);
    assert(!dpool_is_fragmented(pool));

    c = d_malloc(pool, 257);
    assert(c == nullptr);

    /* no free SHM page */
    c = shm_alloc(shm, 1);
    assert(c == nullptr);

    /* free "b" which should release one SHM page */
    d_free(pool, b);
    assert(dpool_is_fragmented(pool));

    c = shm_alloc(shm, 1);
    assert(c != nullptr);


    dpool_destroy(pool);
    shm_close(shm);
}
