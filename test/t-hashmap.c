#include "hashmap.h"

#include <assert.h>

const char key[] = "foo";
char a, b, c;

int main(int argc __attr_unused, char **argv __attr_unused) {
    pool_t pool;
    struct hashmap *map;
    void *p;

    pool = pool_new_libc(NULL, "root");

    map = hashmap_new(pool, 2);
    hashmap_add(map, key, &a);
    hashmap_add(map, key, &b);
    hashmap_add(map, key, &c);

    p = hashmap_get(map, key);
    assert(p == &a);
    p = hashmap_get_next(map, key, p);
    assert(p == &b);
    p = hashmap_get_next(map, key, p);
    assert(p == &c);

    hashmap_remove_value(map, key, &a);
    p = hashmap_get(map, key);
    assert(p == &b);
    p = hashmap_get_next(map, key, p);
    assert(p == &c);

    map = hashmap_new(pool, 2);
    hashmap_add(map, key, &a);
    hashmap_add(map, key, &b);
    hashmap_add(map, key, &c);

    hashmap_remove_value(map, key, &b);
    p = hashmap_get(map, key);
    assert(p == &a);
    p = hashmap_get_next(map, key, p);
    assert(p == &c);

    map = hashmap_new(pool, 2);
    hashmap_add(map, key, &a);
    hashmap_add(map, key, &b);
    hashmap_add(map, key, &c);

    hashmap_remove_value(map, key, &c);
    p = hashmap_get(map, key);
    assert(p == &a);
    p = hashmap_get_next(map, key, p);
    assert(p == &b);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
