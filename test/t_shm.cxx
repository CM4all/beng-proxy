#include "shm/shm.hxx"

#include <inline/compiler.h>

#include <assert.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct shm *shm;
    void *a, *b, *c;

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
}
