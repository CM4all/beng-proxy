/*
 * Hash map with string keys.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hashmap.hxx"
#include "pool.hxx"
#include "util/djbhash.h"
#include "util/Cast.hxx"

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
    struct hashmap *map = (struct hashmap *)
        p_calloc(pool, sizeof(*map) + sizeof(map->slots) * (capacity - 1));
    assert(capacity > 1);
    map->pool = pool;
    map->capacity = capacity;
    return map;
}

bool
hashmap_is_empty(const struct hashmap *map)
{
    for (unsigned i = 0; i < map->capacity; ++i)
        if (map->slots[i].pair.key != nullptr)
            return false;

    return true;
}

gcc_pure
static struct slot *
hashmap_get_slot(struct hashmap *map, const char *key)
{
    assert(map != nullptr);
    assert(key != nullptr);

    return &map->slots[djb_hash_string(key) % map->capacity];
}

gcc_pure
static const struct slot *
hashmap_get_slot_c(const struct hashmap *map, const char *key)
{
    assert(map != nullptr);
    assert(key != nullptr);

    return &map->slots[djb_hash_string(key) % map->capacity];
}

void
hashmap_add(struct hashmap *map, const char *key, void *value)
{
    assert(value != nullptr);

    struct slot *slot = hashmap_get_slot(map, key);
    if (slot->pair.key != nullptr) {
        struct slot *prev = slot;
        slot = NewFromPool<struct slot>(map->pool);
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
    assert(old != nullptr);

    slot->pair.key = key;
    slot->pair.value = value;

    return old;
}

void *
hashmap_set(struct hashmap *map, const char *key, void *value)
{
    assert(key != nullptr);
    assert(value != nullptr);

    struct slot *prev = hashmap_get_slot(map, key);
    if (prev->pair.key == nullptr) {
        prev->pair.key = key;
        prev->pair.value = value;
        return nullptr;
    } else if (strcmp(prev->pair.key, key) == 0)
        return hashmap_overwrite(prev, key, value);

    for (struct slot *slot = prev->next; slot != nullptr; slot = slot->next) {
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

        if (strcmp(slot->pair.key, key) == 0)
            return hashmap_overwrite(slot, key, value);
    }

    auto slot = NewFromPool<struct slot>(map->pool);
    slot->next = prev->next;
    slot->pair.key = key;
    slot->pair.value = value;
    prev->next = slot;
    return nullptr;
}

void *
hashmap_remove(struct hashmap *map, const char *key)
{
    struct slot *prev = hashmap_get_slot(map, key);
    if (prev->pair.key == nullptr)
        return nullptr;

    if (strcmp(prev->pair.key, key) == 0) {
        void *value = prev->pair.value;
        if (prev->next == nullptr) {
            prev->pair.key = nullptr;
            prev->pair.value = nullptr;
        } else {
            struct slot *slot = prev->next;
            *prev = *slot;
            p_free(map->pool, slot);
        }
        return value;
    }

    for (struct slot *slot = prev->next; slot != nullptr;
         prev = slot, slot = prev->next) {
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

        if (strcmp(slot->pair.key, key) == 0) {
            void *value = slot->pair.value;
            prev->next = slot->next;
            p_free(map->pool, slot);
            return value;
        }
    }

    return nullptr;
}

bool
hashmap_remove_value(struct hashmap *map, const char *key, const void *value)
{
    struct slot *prev = hashmap_get_slot(map, key);
    if (prev->pair.key == nullptr)
        return false;

    if (prev->pair.value == value) {
        if (prev->next == nullptr) {
            prev->pair.key = nullptr;
            prev->pair.value = nullptr;
        } else {
            struct slot *slot = prev->next;
            *prev = *slot;
            p_free(map->pool, slot);
        }
        return true;
    }

    for (struct slot *slot = prev->next; slot != nullptr;
         prev = slot, slot = prev->next) {
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

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
    if (prev->pair.key == nullptr)
        return;

    for (struct slot *slot = prev->next; slot != nullptr; slot = prev->next) {
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

        if (strcmp(slot->pair.key, key) == 0 && match(slot->pair.value, ctx)) {
            prev->next = slot->next;
            p_free(map->pool, slot);
        } else
            prev = slot;
    }

    prev = base_slot;
    if (strcmp(prev->pair.key, key) == 0 && match(prev->pair.value, ctx)) {
        if (prev->next == nullptr) {
            prev->pair.key = nullptr;
            prev->pair.value = nullptr;
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

        while ((slot = *slot_r) != nullptr) {
            assert(slot->pair.key != nullptr);

            if (match(slot->pair.key, slot->pair.value, ctx)) {
                *slot_r = slot->next;
                p_free(map->pool, slot);
                ++removed;
            } else
                slot_r = &slot->next;
        }

        /* check the base slot */

        slot = &map->slots[i];
        if (slot->pair.key != nullptr &&
            match(slot->pair.key, slot->pair.value, ctx)) {
            struct slot *next = slot->next;

            if (next == nullptr) {
                /* clear the base slot */
                slot->pair.key = nullptr;
                slot->pair.value = nullptr;
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
    if (slot->pair.key != nullptr && strcmp(slot->pair.key, key) == 0) {
        assert(slot->pair.value != nullptr);
        return slot->pair.value;
    }

    while (slot->next != nullptr) {
        slot = slot->next;
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

        if (strcmp(slot->pair.key, key) == 0)
            return slot->pair.value;
    }

    return nullptr;
}

const struct hashmap_pair *
hashmap_lookup_first(const struct hashmap *map, const char *key)
{
    assert(key != nullptr);

    const struct slot *slot = hashmap_get_slot_c(map, key);
    if (slot->pair.key == nullptr)
        return nullptr;

    do {
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

        if (strcmp(slot->pair.key, key) == 0)
            return &slot->pair;

        slot = slot->next;
    } while (slot != nullptr);

    return nullptr;
}

static const struct slot *
pair_to_slot(const struct hashmap_pair *pair)
{
    return ContainerCast(pair, const struct slot, pair);
}

const struct hashmap_pair *
hashmap_lookup_next(const struct hashmap_pair *prev)
{
    assert(prev != nullptr);
    assert(prev->key != nullptr);
    assert(prev->value != nullptr);

    const char *key = prev->key;

    for (const struct slot *slot = pair_to_slot(prev)->next;
         slot != nullptr; slot = slot->next) {
        assert(slot->pair.key != nullptr);
        assert(slot->pair.value != nullptr);

        if (strcmp(slot->pair.key, key) == 0)
            return &slot->pair;
    }

    return nullptr;
}

void
hashmap_rewind(struct hashmap *map)
{
    map->current_slot = nullptr;
    map->next_slot = 0;
}

const struct hashmap_pair *
hashmap_next(struct hashmap *map)
{
    if (map->current_slot != nullptr && map->current_slot->next != nullptr) {
        map->current_slot = map->current_slot->next;
        return &map->current_slot->pair;
    }

    while (map->next_slot < map->capacity &&
           map->slots[map->next_slot].pair.key == nullptr)
        ++map->next_slot;

    if (map->next_slot >= map->capacity)
        return nullptr;

    map->current_slot = &map->slots[map->next_slot++];
    return &map->current_slot->pair;
}
