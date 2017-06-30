#include "shm/shm.hxx"
#include "shm/dpool.hxx"

#include "util/Compiler.h"

#include <assert.h>
#include <string.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    void *a, *b, *c, *d;

    auto *shm = shm_new(1024, 2);
    assert(shm != nullptr);

    auto *pool = dpool_new(*shm);
    assert(pool != nullptr);

    a = shm_alloc(shm, 1);
    assert(a != nullptr);
    assert(a != pool);

    b = shm_alloc(shm, 1);
    assert(b == nullptr);

    shm_free(shm, a);

    a = d_malloc(*pool, 512);
    assert(a != nullptr);
    memset(a, 0, 512);

    b = d_malloc(*pool, 800);
    assert(b != nullptr);
    memset(b, 0, 800);

    try {
        c = d_malloc(*pool, 512);
        assert(c == nullptr);
    } catch (std::bad_alloc) {
    }

    d = d_malloc(*pool, 220);
    assert(d != nullptr);

    d_free(*pool, a);

    a = d_malloc(*pool, 240);
    assert(a != nullptr);

    try {
        c = d_malloc(*pool, 270);
        assert(c == nullptr);
    } catch (std::bad_alloc) {
    }

    /* no free SHM page */
    c = shm_alloc(shm, 1);
    assert(c == nullptr);

    /* free "b" which should release one SHM page */
    d_free(*pool, b);

    c = shm_alloc(shm, 1);
    assert(c != nullptr);

    dpool_destroy(pool);
    shm_close(shm);
}
