/*
 * Hash map with string keys, stored in mmap (distributed over several
 * worker processes).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "dhashmap.h"
#include "dpool.h"

#include <assert.h>
#include <string.h>

struct slot {
    struct slot *next;
    struct dhashmap_pair pair;
};

struct dhashmap {
    struct dpool *pool;
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

struct dhashmap *
dhashmap_new(struct dpool *pool, unsigned capacity)
{
    struct dhashmap *map;

    assert(capacity > 1);

    map = d_malloc(pool, sizeof(*map) + sizeof(map->slots) * (capacity - 1));
    if (map == NULL)
        return NULL;

    map->pool = pool;
    map->capacity = capacity;

    memset(map->slots, 0, sizeof(map->slots) * capacity);

    return map;
}

void
dhashmap_free(struct dhashmap *map)
{
    /* this function does not delete keys, because these were
       allocated by the caller */

    for (unsigned i = 0; i < map->capacity; ++i) {
        struct slot *slot = map->slots[i].next;

        while (slot != NULL) {
            struct slot *next = slot->next;
            d_free(map->pool, slot);
            slot = next;
        }
    }

    d_free(map->pool, map);
}

static inline void *
dhashmap_overwrite(struct slot *slot, const char *key, void *value)
{
    void *old = slot->pair.value;
    assert(old != NULL);

    slot->pair.key = key;
    slot->pair.value = value;

    return old;
}

void *
dhashmap_put(struct dhashmap *map, const char *key, void *value)
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
        return dhashmap_overwrite(prev, key, value);

    for (slot = prev->next; slot != NULL; slot = slot->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return dhashmap_overwrite(slot, key, value);
    }

    slot = d_malloc(map->pool, sizeof(*slot));
    if (slot == NULL)
        return NULL;

    slot->next = NULL;
    slot->pair.key = key;
    slot->pair.value = value;
    prev->next = slot;
    return NULL;
}

void *
dhashmap_remove(struct dhashmap *map, const char *key)
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
            slot = prev->next;
            *prev = *slot;
            d_free(map->pool, slot);
        }
        return value;
    }

    for (slot = prev->next; slot != NULL; slot = slot->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0) {
            void *value = slot->pair.value;
            prev->next = slot->next;
            d_free(map->pool, slot);
            return value;
        }
    }

    return NULL;
}

void *
dhashmap_get(struct dhashmap *map, const char *key)
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
dhashmap_rewind(struct dhashmap *map)
{
    map->current_slot = NULL;
    map->next_slot = 0;
}

const struct dhashmap_pair *
dhashmap_next(struct dhashmap *map)
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
