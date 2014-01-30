/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hashmap.h"
#include "djbhash.h"
#include "pool.h"

#include <inline/compiler.h>

#include <assert.h>
#include <string.h>

struct slot {
    struct slot *next;
    struct hashmap_pair pair;
};

struct hashmap {
    struct pool *pool;
    unsigned capacity;
    struct slot *current_slot;
    unsigned next_slot;
    struct slot slots[1];
};

struct hashmap *
hashmap_new(struct pool *pool, unsigned capacity)
{
    struct hashmap *map = p_calloc(pool, sizeof(*map) + sizeof(map->slots) * (capacity - 1));
    assert(capacity > 1);
    map->pool = pool;
    map->capacity = capacity;
    return map;
}

bool
hashmap_is_empty(const struct hashmap *map)
{
    for (unsigned i = 0; i < map->capacity; ++i)
        if (map->slots[i].pair.key != NULL)
            return false;

    return true;
}

gcc_pure
static struct slot *
hashmap_get_slot(struct hashmap *map, const char *key)
{
    assert(map != NULL);
    assert(key != NULL);

    return &map->slots[djb_hash_string(key) % map->capacity];
}

gcc_pure
static const struct slot *
hashmap_get_slot_c(const struct hashmap *map, const char *key)
{
    assert(map != NULL);
    assert(key != NULL);

    return &map->slots[djb_hash_string(key) % map->capacity];
}

void
hashmap_add(struct hashmap *map, const char *key, void *value)
{
    assert(value != NULL);

    struct slot *slot = hashmap_get_slot(map, key);
    if (slot->pair.key != NULL) {
        struct slot *prev = slot;
        slot = p_malloc(map->pool, sizeof(*slot));
        slot->next = prev->next;
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
    assert(key != NULL);
    assert(value != NULL);

    struct slot *prev = hashmap_get_slot(map, key);
    if (prev->pair.key == NULL) {
        prev->pair.key = key;
        prev->pair.value = value;
        return NULL;
    } else if (strcmp(prev->pair.key, key) == 0)
        return hashmap_overwrite(prev, key, value);

    for (struct slot *slot = prev->next; slot != NULL; slot = slot->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return hashmap_overwrite(slot, key, value);
    }

    struct slot *slot = p_malloc(map->pool, sizeof(*slot));
    slot->next = prev->next;
    slot->pair.key = key;
    slot->pair.value = value;
    prev->next = slot;
    return NULL;
}

void *
hashmap_remove(struct hashmap *map, const char *key)
{
    struct slot *prev = hashmap_get_slot(map, key);
    if (prev->pair.key == NULL)
        return NULL;

    if (strcmp(prev->pair.key, key) == 0) {
        void *value = prev->pair.value;
        if (prev->next == NULL) {
            prev->pair.key = NULL;
            prev->pair.value = NULL;
        } else {
            struct slot *slot = prev->next;
            *prev = *slot;
            p_free(map->pool, slot);
        }
        return value;
    }

    for (struct slot *slot = prev->next; slot != NULL;
         prev = slot, slot = prev->next) {
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
    struct slot *prev = hashmap_get_slot(map, key);
    if (prev->pair.key == NULL)
        return false;

    if (prev->pair.value == value) {
        if (prev->next == NULL) {
            prev->pair.key = NULL;
            prev->pair.value = NULL;
        } else {
            struct slot *slot = prev->next;
            *prev = *slot;
            p_free(map->pool, slot);
        }
        return true;
    }

    for (struct slot *slot = prev->next; slot != NULL;
         prev = slot, slot = prev->next) {
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

void
hashmap_remove_existing(struct hashmap *map, const char *key,
                        const void *value)
{
#ifndef NDEBUG
    bool found =
#endif
        hashmap_remove_value(map, key, value);
    assert(found);
}

void
hashmap_remove_match(struct hashmap *map, const char *key,
                     bool (*match)(void *value, void *ctx), void *ctx)
{
    struct slot *const base_slot = hashmap_get_slot(map, key);

    struct slot *prev = base_slot;
    if (prev->pair.key == NULL)
        return;

    for (struct slot *slot = prev->next; slot != NULL; slot = prev->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0 && match(slot->pair.value, ctx)) {
            prev->next = slot->next;
            p_free(map->pool, slot);
        } else
            prev = slot;
    }

    prev = base_slot;
    if (strcmp(prev->pair.key, key) == 0 && match(prev->pair.value, ctx)) {
        if (prev->next == NULL) {
            prev->pair.key = NULL;
            prev->pair.value = NULL;
        } else {
            struct slot *slot = prev->next;
            *prev = *slot;
            p_free(map->pool, slot);
        }
    }
}

unsigned
hashmap_remove_all_match(struct hashmap *map,
                         bool (*match)(const char *key, void *value,
                                       void *ctx),
                         void *ctx)
{
    unsigned removed = 0;

    for (unsigned i = 0; i < map->capacity; ++i) {
        struct slot **slot_r = &map->slots[i].next, *slot;

        /* check the slot chain */

        while ((slot = *slot_r) != NULL) {
            assert(slot->pair.key != NULL);

            if (match(slot->pair.key, slot->pair.value, ctx)) {
                *slot_r = slot->next;
                p_free(map->pool, slot);
                ++removed;
            } else
                slot_r = &slot->next;
        }

        /* check the base slot */

        slot = &map->slots[i];
        if (slot->pair.key != NULL &&
            match(slot->pair.key, slot->pair.value, ctx)) {
            struct slot *next = slot->next;

            if (next == NULL) {
                /* clear the base slot */
                slot->pair.key = NULL;
                slot->pair.value = NULL;
            } else {
                /* overwrite the base slot with the first chained
                   item */
                *slot = *next;
                p_free(map->pool, next);
            }

            ++removed;
        }
    }

    return removed;
}

void *
hashmap_get(const struct hashmap *map, const char *key)
{
    const struct slot *slot = hashmap_get_slot_c(map, key);
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

const struct hashmap_pair *
hashmap_lookup_first(const struct hashmap *map, const char *key)
{
    assert(key != NULL);

    const struct slot *slot = hashmap_get_slot_c(map, key);
    if (slot->pair.key == NULL)
        return NULL;

    do {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return &slot->pair;

        slot = slot->next;
    } while (slot != NULL);

    return NULL;
}

static const struct slot *
pair_to_slot(const struct hashmap_pair *pair)
{
    return (const struct slot*)(((const char*)pair) - offsetof(struct slot, pair));
}

const struct hashmap_pair *
hashmap_lookup_next(const struct hashmap_pair *prev)
{
    assert(prev != NULL);
    assert(prev->key != NULL);
    assert(prev->value != NULL);

    const char *key = prev->key;

    for (const struct slot *slot = pair_to_slot(prev)->next;
         slot != NULL; slot = slot->next) {
        assert(slot->pair.key != NULL);
        assert(slot->pair.value != NULL);

        if (strcmp(slot->pair.key, key) == 0)
            return &slot->pair;
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
