/*
 * String hash map.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strmap.h"
#include "compiler.h"

#include <assert.h>
#include <string.h>

struct slot {
    struct slot *next;
    struct pair pair;
};

struct strmap {
    pool_t pool;
    unsigned capacity;
    struct slot *current_slot;
    unsigned next_slot;
    struct slot slots[1];
};

static inline unsigned
calc_hash(const char *p) {
    unsigned hash = 5381;

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
    struct slot *slot, *prev;

    assert(value != NULL);

    slot = &map->slots[hash % map->capacity];
    if (slot->pair.key != NULL) {
        while (slot->next != NULL) {
            slot = slot->next;
            assert(slot->pair.key != NULL);
            assert(slot->pair.value != NULL);
        }

        prev = slot;
        slot = p_malloc(map->pool, sizeof(*slot));
        slot->next = NULL;
        prev->next = slot;
    }

    slot->pair.key = key;
    slot->pair.value = value;
}

static inline const char *
strmap_maybe_overwrite(struct slot *slot, const char *value, int overwrite)
{
    const char *old = slot->pair.value;
    assert(old != NULL);
    if (overwrite)
        slot->pair.value = value;
    return old;
}

const char *
strmap_put(strmap_t map, const char *key, const char *value, int overwrite)
{
    unsigned hash = calc_hash(key);
    struct slot *slot, *prev;

    assert(key != NULL);
    assert(value != NULL);

    prev = &map->slots[hash % map->capacity];
    if (prev->pair.key != NULL && strcmp(prev->pair.key, key) == 0)
        return strmap_maybe_overwrite(prev, value, overwrite);

    for (slot = prev->next; slot != NULL; slot = prev->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return strmap_maybe_overwrite(slot, value, overwrite);
    }

    slot = p_malloc(map->pool, sizeof(*slot));
    slot->next = NULL;
    slot->pair.key = key;
    slot->pair.value = value;
    prev->next = slot;
    return NULL;
}

const char *
strmap_get(strmap_t map, const char *key)
{
    unsigned hash = calc_hash(key);
    struct slot *slot;

    slot = &map->slots[hash % map->capacity];
    if (slot->pair.key != NULL && strcmp(slot->pair.key, key) == 0) {
        assert(slot->pair.value != NULL);
        return slot->pair.value;
    }

    while (slot->next != NULL) {
        slot = slot->next;
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return slot->pair.value;
    }

    return NULL;
}

void
strmap_rewind(strmap_t map)
{
    map->current_slot = NULL;
    map->next_slot = 0;
}

const struct pair *
strmap_next(strmap_t map)
{
    if (map->current_slot != NULL && map->current_slot->next != NULL) {
        map->current_slot = map->current_slot->next;
        return &map->current_slot->pair;
    }

    while (map->next_slot < map->capacity &&
           map->slots[map->next_slot].pair.key == NULL)
        ++map->next_slot;

    if (map->next_slot >= map->capacity)
        return NULL;

    map->current_slot = &map->slots[map->next_slot++];
    return &map->current_slot->pair;
}
