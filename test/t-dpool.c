#include "shm.h"
#include "dpool.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    struct shm *shm;
    struct dpool *pool;
    void *a, *b, *c;

    shm = shm_new(1024, 2);
    assert(shm != NULL);

    pool = dpool_new(shm);
    assert(pool != NULL);

    a = shm_alloc(shm);
    assert(a != NULL);
    assert(a != pool);

    b = shm_alloc(shm);
    assert(b == NULL);

    shm_free(shm, a);

    a = d_malloc(pool, 512);
    assert(a != NULL);
    memset(a, 0, 512);

    b = d_malloc(pool, 512);
    assert(b != NULL);
    memset(b, 0, 512);

    c = d_malloc(pool, 512);
    assert(c == NULL);

    dpool_destroy(pool);
    shm_close(shm);
}
