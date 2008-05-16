#include "shm.h"

#include <inline/compiler.h>

#include <assert.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    struct shm *shm;
    void *a, *b, *c;

    shm = shm_new(1024, 2);

    a = shm_alloc(shm, 1);
    assert(a != NULL);

    b = shm_alloc(shm, 2);
    assert(b == NULL);

    b = shm_alloc(shm, 1);
    assert(b != NULL);

    c = shm_alloc(shm, 1);
    assert(c == NULL);

    shm_free(shm, a);
    c = shm_alloc(shm, 1);
    assert(c != NULL);

    a = shm_alloc(shm, 1);
    assert(a == NULL);

    shm_free(shm, b);
    shm_free(shm, c);

    a = shm_alloc(shm, 2);
    assert(a != NULL);

    b = shm_alloc(shm, 2);
    assert(b == NULL);

    b = shm_alloc(shm, 1);
    assert(b == NULL);

    shm_free(shm, a);

    a = shm_alloc(shm, 2);
    assert(a != NULL);

    shm_close(shm);
}
