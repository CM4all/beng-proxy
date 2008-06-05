#include "shm.h"
#include "dpool.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    struct shm *shm;
    struct dpool *pool;
    void *a, *b, *c, *d;

    shm = shm_new(1024, 2);
    assert(shm != NULL);

    pool = dpool_new(shm);
    assert(pool != NULL);
    assert(!dpool_is_fragmented(pool));

    a = shm_alloc(shm, 1);
    assert(a != NULL);
    assert(a != pool);

    b = shm_alloc(shm, 1);
    assert(b == NULL);

    shm_free(shm, a);

    a = d_malloc(pool, 512);
    assert(a != NULL);
    memset(a, 0, 512);
    assert(!dpool_is_fragmented(pool));

    b = d_malloc(pool, 800);
    assert(b != NULL);
    memset(b, 0, 800);
    assert(!dpool_is_fragmented(pool));

    c = d_malloc(pool, 512);
    assert(c == NULL);

    d = d_malloc(pool, 220);
    assert(d != NULL);
    assert(!dpool_is_fragmented(pool));

    d_free(pool, a);
    assert(dpool_is_fragmented(pool));

    a = d_malloc(pool, 220);
    assert(a != NULL);
    assert(!dpool_is_fragmented(pool));

    c = d_malloc(pool, 257);
    assert(c == NULL);

    /* no free SHM page */
    c = shm_alloc(shm, 1);
    assert(c == NULL);

    /* free "b" which should release one SHM page */
    d_free(pool, b);
    assert(dpool_is_fragmented(pool));

    c = shm_alloc(shm, 1);
    assert(c != NULL);


    dpool_destroy(pool);
    shm_close(shm);
}
