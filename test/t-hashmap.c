#include "hashmap.h"
#include "pool.h"

#include <assert.h>

const char key[] = "foo";
char a, b, c;

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct pool *pool;
    struct hashmap *map;
    void *p;
    const struct hashmap_pair *pair;

    pool = pool_new_libc(NULL, "root");

    map = hashmap_new(pool, 2);
    hashmap_add(map, key, &a);
    hashmap_add(map, key, &b);
    hashmap_add(map, key, &c);

    p = hashmap_get(map, key);
    assert(p == &a);

    pair = hashmap_lookup_first(map, key);
    assert(pair->value == &a);
    pair = hashmap_lookup_next(pair);
    assert(pair->value == &c);
    pair = hashmap_lookup_next(pair);
    assert(pair->value == &b);
    pair = hashmap_lookup_next(pair);
    assert(pair == NULL);

    hashmap_remove_value(map, key, &a);
    p = hashmap_get(map, key);
    assert(p == &c);

    pair = hashmap_lookup_first(map, key);
    assert(pair->value == &c);
    pair = hashmap_lookup_next(pair);
    assert(pair->value == &b);
    pair = hashmap_lookup_next(pair);
    assert(pair == NULL);

    map = hashmap_new(pool, 2);
    hashmap_add(map, key, &a);
    hashmap_add(map, key, &b);
    hashmap_add(map, key, &c);

    hashmap_remove_value(map, key, &b);
    p = hashmap_get(map, key);
    assert(p == &a);

    pair = hashmap_lookup_first(map, key);
    assert(pair->value == &a);
    pair = hashmap_lookup_next(pair);
    assert(pair->value == &c);
    pair = hashmap_lookup_next(pair);
    assert(pair == NULL);

    map = hashmap_new(pool, 2);
    hashmap_add(map, key, &a);
    hashmap_add(map, key, &b);
    hashmap_add(map, key, &c);

    hashmap_remove_value(map, key, &c);
    p = hashmap_get(map, key);
    assert(p == &a);

    pair = hashmap_lookup_first(map, key);
    assert(pair->value == &a);
    pair = hashmap_lookup_next(pair);
    assert(pair->value == &b);
    pair = hashmap_lookup_next(pair);
    assert(pair == NULL);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
