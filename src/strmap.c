/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.h"

#include <assert.h>
#include <string.h>

struct pair {
    struct pair *next;
    const char *key, *value;
};

struct strmap {
    pool_t pool;
    unsigned capacity;
    struct pair slots[1];
};

static unsigned
calc_hash(const char *p) {
    unsigned hash = 0;

    assert(p != NULL);

    while (*p != 0)
        hash = (hash << 5) + hash + *p++;

    return hash;
}

strmap_t
strmap_new(pool_t pool, unsigned capacity)
{
    strmap_t map = p_calloc(pool, sizeof(*map) + sizeof(map->slots) * (capacity - 1));
    assert(capacity > 1);
    map->pool = pool;
    map->capacity = capacity;
    return map;
}

void
strmap_addn(strmap_t map, const char *key, const char *value)
{
    unsigned hash = calc_hash(key);
    struct pair *slot, *prev;

    assert(value != NULL);

    slot = &map->slots[hash % map->capacity];
    if (slot->key != NULL) {
        while (slot->next != NULL) {
            slot = slot->next;
            assert(slot->key != NULL);
            assert(slot->value != NULL);
        }

        prev = slot;
        slot = p_malloc(map->pool, sizeof(*slot));
        slot->next = NULL;
        prev->next = slot;
    }

    slot->key = key;
    slot->value = value;
}

const char *
strmap_get(strmap_t map, const char *key)
{
    unsigned hash = calc_hash(key);
    struct pair *slot;

    slot = &map->slots[hash % map->capacity];
    if (slot->key != NULL && strcmp(slot->key, key) == 0) {
        assert(slot->value != NULL);
        return slot->value;
    }

    while (slot->next != NULL) {
        slot = slot->next;
        assert(slot->key != NULL);
        assert(slot->value != NULL);

        if (strcmp(slot->key, key) == 0)
            return slot->value;
    }

    return NULL;
}
