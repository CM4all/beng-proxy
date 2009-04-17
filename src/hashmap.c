/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hashmap.h"

#include <inline/compiler.h>

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

struct hashmap *
hashmap_new(pool_t pool, unsigned capacity)
{
    struct hashmap *map = p_calloc(pool, sizeof(*map) + sizeof(map->slots) * (capacity - 1));
    assert(capacity > 1);
    map->pool = pool;
    map->capacity = capacity;
    return map;
}

void
hashmap_add(struct hashmap *map, const char *key, void *value)
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
hashmap_overwrite(struct slot *slot, const char *key, void *value)
{
    void *old = slot->pair.value;
    assert(old != NULL);

    slot->pair.key = key;
    slot->pair.value = value;

    return old;
}

void *
hashmap_set(struct hashmap *map, const char *key, void *value)
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
        return hashmap_overwrite(prev, key, value);

    for (slot = prev->next; slot != NULL; slot = slot->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return hashmap_overwrite(slot, key, value);
    }

    slot = p_malloc(map->pool, sizeof(*slot));
    slot->next = prev->next;
    slot->pair.key = key;
    slot->pair.value = value;
    prev->next = slot;
    return NULL;
}

void *
hashmap_remove(struct hashmap *map, const char *key)
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
            p_free(map->pool, slot);
        }
        return value;
    }

    for (slot = prev->next; slot != NULL; prev = slot, slot = prev->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0) {
            void *value = slot->pair.value;
            prev->next = slot->next;
            p_free(map->pool, slot);
            return value;
        }
    }

    return NULL;
}

bool
hashmap_remove_value(struct hashmap *map, const char *key, const void *value)
{
    unsigned hash = calc_hash(key);
    struct slot *slot, *prev;

    assert(key != NULL);

    prev = &map->slots[hash % map->capacity];
    assert(prev->pair.key != NULL);

    if (prev->pair.value == value) {
        if (prev->next == NULL) {
            prev->pair.key = NULL;
            prev->pair.value = NULL;
        } else {
            slot = prev->next;
            *prev = *slot;
            p_free(map->pool, slot);
        }
        return true;
    }

    for (slot = prev->next; slot != NULL; prev = slot, slot = prev->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (slot->pair.value == value) {
            prev->next = slot->next;
            p_free(map->pool, slot);
            return true;
        }
    }

    /* not found */
    return false;
}

void *
hashmap_get(struct hashmap *map, const char *key)
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

static struct slot *
hashmap_find_value(struct slot *slot, const void *value)
{
    assert(slot != NULL);

    while (slot->pair.value != value) {
        slot = slot->next;

        assert(slot != NULL);
    }

    return slot;
}

void *
hashmap_get_next(struct hashmap *map, const char *key, const void *prev)
{
    unsigned hash = calc_hash(key);
    struct slot *slot;

    slot = hashmap_find_value(&map->slots[hash % map->capacity], prev);

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
hashmap_rewind(struct hashmap *map)
{
    map->current_slot = NULL;
    map->next_slot = 0;
}

const struct hashmap_pair *
hashmap_next(struct hashmap *map)
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
