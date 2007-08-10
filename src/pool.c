/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.h"
#include "list.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LINEAR_ALIGN 8

enum pool_type {
    POOL_LIBC,
    POOL_LINEAR,
};

struct libc_pool_chunk {
    struct libc_pool_chunk *next;
    unsigned char data[1];
};

struct linear_pool_area {
    struct linear_pool_area *prev;
    size_t size, used;
    unsigned char data[1];
};

struct pool {
    struct list_head siblings, children;
    pool_t parent;
    unsigned ref;
    enum pool_type type;
    const char *name;
    union {
        struct libc_pool_chunk *libc;
        struct linear_pool_area *linear;
    } current_area;
};

static struct {
    unsigned num_linear_areas;
    struct linear_pool_area *linear_areas;
} recycler;

static void *
xmalloc(size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

static void *
xcalloc(size_t size)
{
    void *p = calloc(1, size);
    if (p == NULL) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

void
pool_recycler_clear(void)
{
    struct linear_pool_area *linear;

    while (recycler.linear_areas != NULL) {
        linear = recycler.linear_areas;
        recycler.linear_areas = linear->prev;
        free(linear);
    }

    recycler.num_linear_areas = 0;
}

static void
pool_recycler_put_linear(struct linear_pool_area *area)
{
    assert(area != 0);
    assert(area->size > 0);

    if (recycler.num_linear_areas < 32) {
        area->prev = recycler.linear_areas;
        recycler.linear_areas = area;
        ++recycler.num_linear_areas;
    } else {
        free(area);
    }
}

static struct linear_pool_area *
pool_recycler_get_linear(size_t size)
{
    struct linear_pool_area **linear_p, *linear;

    assert(size > 0);

    for (linear_p = &recycler.linear_areas, linear = *linear_p;
         linear != NULL;
         linear_p = &linear->prev, linear = *linear_p) {
        if (linear->size == size) {
            assert(recycler.num_linear_areas > 0);
            --recycler.num_linear_areas;
            *linear_p = linear->prev;
            return linear;
        }
    }

    return NULL;
}

static void
pool_add_child(pool_t pool, pool_t child)
{
    assert(child->parent == NULL);

    child->parent = pool;
    list_add(&child->siblings, &pool->children);
}

static pool_t
pool_new(pool_t parent, const char *name)
{
    pool_t pool;

    pool = xcalloc(sizeof(*pool));
    list_init(&pool->children);
    pool->ref = 1;
    pool->name = name;

    if (parent != NULL)
        pool_add_child(parent, pool);

    return pool;
}

pool_t
pool_new_libc(pool_t parent, const char *name)
{
    pool_t pool = pool_new(parent, name);
    pool->type = POOL_LIBC;
    return pool;
}

static struct linear_pool_area *
pool_new_linear_area(struct linear_pool_area *prev, size_t size)
{
    struct linear_pool_area *area = xmalloc(sizeof(*area) - sizeof(area->data) + size);
    if (area == NULL)
        abort();
    area->prev = prev;
    area->size = size;
    area->used = 0;
    return area;
}

static struct linear_pool_area *
pool_get_linear_area(struct linear_pool_area *prev, size_t size)
{
    struct linear_pool_area *area = pool_recycler_get_linear(size);
    if (area == NULL) {
        area = pool_new_linear_area(prev, size);
    } else {
        area->prev = prev;
        area->used = 0;
    }
    return area;
}

pool_t
pool_new_linear(pool_t parent, const char *name, size_t initial_size)
{
    pool_t pool = pool_new(parent, name);
    pool->type = POOL_LINEAR;

    pool->current_area.linear = pool_get_linear_area(NULL, initial_size);

    assert(parent != NULL);

    return pool;
}

static void
pool_destroy(pool_t pool)
{
    assert(list_empty(&pool->children));

    if (pool->parent != NULL)
        list_remove(&pool->siblings);

    switch (pool->type) {
    case POOL_LIBC:
        while (pool->current_area.libc != NULL) {
            struct libc_pool_chunk *chunk = pool->current_area.libc;
            pool->current_area.libc = chunk->next;
            free(chunk);
        }
        break;

    case POOL_LINEAR:
        while (pool->current_area.linear != NULL) {
            struct linear_pool_area *area = pool->current_area.linear;
            pool->current_area.linear = area->prev;
            pool_recycler_put_linear(area);
        }
        break;
    }

    free(pool);
}

void
pool_ref(pool_t pool)
{
    assert(pool->ref > 0);
    ++pool->ref;
}

void
pool_unref(pool_t pool)
{
    assert(pool->ref > 0);
    --pool->ref;
    if (pool->ref == 0)
        pool_destroy(pool);
}

static void *
p_malloc_libc(pool_t pool, size_t size)
{
    struct libc_pool_chunk *chunk = xmalloc(sizeof(*chunk) - sizeof(chunk->data) + size);
    chunk->next = pool->current_area.libc;
    pool->current_area.libc = chunk;
    return chunk->data;
}

static void *
p_malloc_linear(pool_t pool, size_t size)
{
    struct linear_pool_area *area = pool->current_area.linear;
    void *p;

    size = ((size + LINEAR_ALIGN - 1) / LINEAR_ALIGN) * LINEAR_ALIGN;

    if (area->used + size > area->size) {
        size_t new_area_size = area->size;
        fprintf(stderr, "growing linear pool '%s'\n", pool->name);
        if (size > new_area_size)
            new_area_size = ((size + new_area_size - 1) / new_area_size) * new_area_size;
        area = pool_get_linear_area(area, new_area_size);
        pool->current_area.linear = area;
    }

    p = area->data + area->used;
    area->used += size;

    return p;
}

void *
p_malloc(pool_t pool, size_t size)
{
    switch (pool->type) {
    case POOL_LIBC:
        return p_malloc_libc(pool, size);

    case POOL_LINEAR:
        return p_malloc_linear(pool, size);
    }

    assert(0);
    return NULL;
}

void *
p_calloc(pool_t pool, size_t size)
{
    void *p = p_malloc(pool, size);
    memset(p, 0, size);
    return p;
}

char *
p_strdup(pool_t pool, const char *src)
{
    size_t length = strlen(src) + 1;
    char *dest = p_malloc(pool, length);
    memcpy(dest, src, length);
    return dest;
}

char *
p_strndup(pool_t pool, const char *src, size_t length)
{
    char *dest = p_malloc(pool, length + 1);
    memcpy(dest, src, length);
    dest[length] = 0;
    return dest;
}
