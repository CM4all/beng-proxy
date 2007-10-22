/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hashmap.h"
#include "compiler.h"

#include <assert.h>
#include <string.h>

struct slot {
    struct slot *next;
    struct hashmap_pair pair;
};

struct hashmap {
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

hashmap_t
hashmap_new(pool_t pool, unsigned capacity)
{
    hashmap_t map = p_calloc(pool, sizeof(*map) + sizeof(map->slots) * (capacity - 1));
    assert(capacity > 1);
    map->pool = pool;
    map->capacity = capacity;
    return map;
}

void
hashmap_addn(hashmap_t map, const char *key, void *value)
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

static inline void *
hashmap_maybe_overwrite(struct slot *slot, void *value, int overwrite)
{
    void *old = slot->pair.value;
    assert(old != NULL);
    if (overwrite)
        slot->pair.value = value;
    return old;
}

void *
hashmap_put(hashmap_t map, const char *key, void *value, int overwrite)
{
    unsigned hash = calc_hash(key);
    struct slot *slot, *prev;

    assert(key != NULL);
    assert(value != NULL);

    prev = &map->slots[hash % map->capacity];
    if (prev->pair.key == NULL) {
        prev->pair.key = key;
        prev->pair.value = value;
        return NULL;
    } else if (strcmp(prev->pair.key, key) == 0)
        return hashmap_maybe_overwrite(prev, value, overwrite);

    for (slot = prev->next; slot != NULL; slot = prev->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return hashmap_maybe_overwrite(slot, value, overwrite);
    }

    slot = p_malloc(map->pool, sizeof(*slot));
    slot->next = NULL;
    slot->pair.key = key;
    slot->pair.value = value;
    prev->next = slot;
    return NULL;
}

void *
hashmap_remove(hashmap_t map, const char *key)
{
    unsigned hash = calc_hash(key);
    struct slot *slot, *prev;

    assert(key != NULL);

    prev = &map->slots[hash % map->capacity];
    if (prev->pair.key == NULL)
        return NULL;

    if (strcmp(prev->pair.key, key) == 0) {
        void *value = prev->pair.value;
        if (prev->next == NULL) {
            prev->pair.key = NULL;
            prev->pair.value = NULL;
        } else {
            *prev = *prev->next;
        }
        return value;
    }

    for (slot = prev->next; slot != NULL; slot = prev->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0) {
            prev->next = slot->next;
            return slot->pair.value;
        }
    }

    return NULL;
}

void *
hashmap_get(hashmap_t map, const char *key)
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
hashmap_rewind(hashmap_t map)
{
    map->current_slot = NULL;
    map->next_slot = 0;
}

const struct hashmap_pair *
hashmap_next(hashmap_t map)
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
