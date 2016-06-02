#include "shm/shm.hxx"

#include <inline/compiler.h>

#include <assert.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct shm *shm;
    void *a, *b, *c, *d, *e;

    shm = shm_new(1024, 2);

    a = shm_alloc(shm, 1);
    assert(a != nullptr);

    b = shm_alloc(shm, 2);
    assert(b == nullptr);

    b = shm_alloc(shm, 1);
    assert(b != nullptr);

    c = shm_alloc(shm, 1);
    assert(c == nullptr);

    shm_free(shm, a);
    c = shm_alloc(shm, 1);
    assert(c != nullptr);

    a = shm_alloc(shm, 1);
    assert(a == nullptr);

    shm_free(shm, b);
    shm_free(shm, c);

    a = shm_alloc(shm, 2);
    assert(a != nullptr);

    b = shm_alloc(shm, 2);
    assert(b == nullptr);

    b = shm_alloc(shm, 1);
    assert(b == nullptr);

    shm_free(shm, a);

    a = shm_alloc(shm, 2);
    assert(a != nullptr);

    shm_close(shm);

    /* allocate and deallocate in different order, to see if adjacent
       free pages are merged properly */

    shm = shm_new(1024, 5);

    a = shm_alloc(shm, 1);
    assert(a != nullptr);

    b = shm_alloc(shm, 2);
    assert(b != nullptr);

    c = shm_alloc(shm, 1);
    assert(c != nullptr);

    d = shm_alloc(shm, 1);
    assert(d != nullptr);

    e = shm_alloc(shm, 1);
    assert(e == nullptr);

    shm_free(shm, b);
    shm_free(shm, c);

    e = shm_alloc(shm, 4);
    assert(e == nullptr);

    e = shm_alloc(shm, 3);
    assert(e != nullptr);
    shm_free(shm, e);

    b = shm_alloc(shm, 2);
    assert(b != nullptr);

    c = shm_alloc(shm, 1);
    assert(c != nullptr);

    shm_free(shm, c);
    shm_free(shm, b);

    e = shm_alloc(shm, 4);
    assert(e == nullptr);

    e = shm_alloc(shm, 3);
    assert(e != nullptr);
    shm_free(shm, e);

    shm_close(shm);
}
