#include "shm.h"

#include <inline/compiler.h>

#include <assert.h>

int main(int argc __attr_unused, char **argv __attr_unused) {
    struct shm *shm;
    void *a, *b, *c;

    shm = shm_new(1024, 2);

    a = shm_alloc(shm);
    assert(a != NULL);

    b = shm_alloc(shm);
    assert(b != NULL);

    c = shm_alloc(shm);
    assert(c == NULL);

    shm_free(shm, a);
    c = shm_alloc(shm);
    assert(c != NULL);

    a = shm_alloc(shm);
    assert(a == NULL);

    shm_close(shm);
}
