/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.h"
#include "list.h"
#include "compiler.h"

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#if defined(__x86_64__) || defined(__PPC64__)
#define ALIGN 8
#define ALIGN_BITS 0x7
#else
#define ALIGN 4
#define ALIGN_BITS 0x3
#endif

#define RECYCLER_MAX_POOLS 256
#define RECYCLER_MAX_LINEAR_AREAS 256
#define RECYCLER_MAX_LINEAR_SIZE 65536

enum pool_type {
    POOL_LIBC,
    POOL_LINEAR,
};

struct libc_pool_chunk {
    struct libc_pool_chunk *next;
    unsigned char data[sizeof(size_t)];
};

struct linear_pool_area {
    struct linear_pool_area *prev;
    size_t size, used;
    unsigned char data[sizeof(size_t)];
};

struct pool_ref {
    struct list_head list_head;
    const char *file;
    unsigned line;
    unsigned count;
};

struct pool {
    struct list_head siblings, children;
#ifdef DEBUG_POOL_REF
    struct list_head refs, unrefs;
    struct pool_ref main_ref;
#endif
    pool_t parent;
    unsigned ref;
    int trashed;
    enum pool_type type;
    const char *name;
    union {
        struct libc_pool_chunk *libc;
        struct linear_pool_area *linear;
        struct pool *recycler;
    } current_area;
};

#ifndef NDEBUG
static LIST_HEAD(trash);
#endif

static struct {
    unsigned num_pools;
    pool_t pools;
    unsigned num_linear_areas;
    struct linear_pool_area *linear_areas;
} recycler;

static void * attr_malloc
xmalloc(size_t size)
{
    void *p = malloc(size);
    if (unlikely(p == NULL)) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

static inline size_t attr_const
align_size(size_t size)
{
    return ((size - 1) | ALIGN_BITS) + 1;
}

void
pool_recycler_clear(void)
{
    pool_t pool;
    struct linear_pool_area *linear;

    while (recycler.pools != NULL) {
        pool = recycler.pools;
        recycler.pools = pool->current_area.recycler;
        free(pool);
    }

    recycler.num_pools = 0;

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

#ifdef POISON
    memset(area->data, 0x01, area->used);
#endif

    if (recycler.num_linear_areas < RECYCLER_MAX_LINEAR_AREAS &&
        area->size <= RECYCLER_MAX_LINEAR_SIZE) {
#ifdef VALGRIND
        VALGRIND_MAKE_MEM_NOACCESS(area->data, area->size);
#endif

        area->prev = recycler.linear_areas;
        recycler.linear_areas = area;
        ++recycler.num_linear_areas;
    } else {
#ifdef VALGRIND
        VALGRIND_MAKE_MEM_UNDEFINED(area, sizeof(*area) - sizeof(area->data) + area->size);
#endif

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

static inline void
pool_add_child(pool_t pool, pool_t child)
{
    assert(child->parent == NULL);

    child->parent = pool;
    list_add(&child->siblings, &pool->children);
}

static inline void
pool_remove_child(pool_t pool, pool_t child)
{
    (void)pool;

    assert(child->parent == pool);

    list_remove(&child->siblings);
    child->parent = NULL;
}

static pool_t attr_malloc
pool_new(pool_t parent, const char *name)
{
    pool_t pool;

    if (recycler.pools == NULL)
        pool = xmalloc(sizeof(*pool));
    else {
        pool = recycler.pools;
        recycler.pools = pool->current_area.recycler;
        --recycler.num_pools;
    }

    list_init(&pool->children);
#ifdef DEBUG_POOL_REF
    list_init(&pool->refs);
    list_init(&pool->unrefs);
#endif
    pool->ref = 1;
    pool->trashed = 0;
    pool->name = name;

    pool->parent = NULL;
    if (parent != NULL)
        pool_add_child(parent, pool);

    return pool;
}

pool_t
pool_new_libc(pool_t parent, const char *name)
{
    pool_t pool = pool_new(parent, name);
    pool->type = POOL_LIBC;
    pool->current_area.libc = NULL;
    return pool;
}

static struct linear_pool_area * attr_malloc
pool_new_linear_area(struct linear_pool_area *prev, size_t size)
{
    struct linear_pool_area *area = xmalloc(sizeof(*area) - sizeof(area->data) + size);
    if (area == NULL)
        abort();
    area->prev = prev;
    area->size = size;
    area->used = 0;

#ifdef POISON
    memset(area->data, 0x01, area->size);
#endif

#ifdef VALGRIND
    VALGRIND_MAKE_MEM_NOACCESS(area->data, area->size);
#endif

    return area;
}

static inline struct linear_pool_area *
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
    assert(pool->ref == 0);
    assert(pool->parent == NULL);

#ifndef NDEBUG
    if (pool->trashed)
        list_remove(&pool->siblings);

    while (!list_empty(&pool->children)) {
        pool_t child = (pool_t)pool->children.next;
        pool_remove_child(pool, child);
        assert(child->ref > 0);
        list_add(&child->siblings, &trash);
        child->trashed = 1;
    }
#endif

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

    if (recycler.num_pools < RECYCLER_MAX_POOLS) {
        pool->current_area.recycler = recycler.pools;
        recycler.pools = pool;
        ++recycler.num_pools;
    } else
        free(pool);
}

#ifdef DEBUG_POOL_REF
static void
pool_increment_ref(pool_t pool, struct list_head *list,
                   const char *file, unsigned line)
{
    struct pool_ref *ref;

    for (ref = (struct pool_ref *)&pool->refs.next;
         &ref->list_head != &pool->refs;
         ref = (struct pool_ref *)&ref->list_head.next) {
        if (ref->line == line && strcmp(ref->file, file) == 0) {
            ++ref->count;
            return;
        }
    }

    ref = p_malloc(pool, sizeof(*ref));
    ref->file = file;
    ref->line = line;
    ref->count = 1;
    list_add(&ref->list_head, list);
}
#endif

#ifdef DEBUG_POOL_REF
static void
pool_dump_refs(pool_t pool)
{
    const struct pool_ref *ref;
    fprintf(stderr, "pool '%s'(%u) REF:\n", pool->name, pool->ref);
    for (ref = (const struct pool_ref *)pool->refs.next;
         &ref->list_head != &pool->refs;
         ref = (const struct pool_ref *)ref->list_head.next) {
        fprintf(stderr, "\t%s:%u %u\n", ref->file, ref->line, ref->count);
    }
    fprintf(stderr, "    UNREF:\n");
    for (ref = (const struct pool_ref *)pool->unrefs.next;
         &ref->list_head != &pool->unrefs;
         ref = (const struct pool_ref *)ref->list_head.next) {
        fprintf(stderr, "\t%s:%u %u\n", ref->file, ref->line, ref->count);
    }
}
#endif

void
#ifdef DEBUG_POOL_REF
pool_ref_debug(pool_t pool, const char *file, unsigned line)
#else
pool_ref(pool_t pool)
#endif
{
    assert(pool->ref > 0);
    ++pool->ref;

#ifdef POOL_TRACE_REF
    fprintf(stderr, "pool_ref('%s')=%u\n", pool->name, pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, &pool->refs, file, line);
#endif
}

unsigned
#ifdef DEBUG_POOL_REF
pool_unref_debug(pool_t pool, const char *file, unsigned line)
#else
pool_unref(pool_t pool)
#endif
{
    assert(pool->ref > 0);
    --pool->ref;

#ifdef POOL_TRACE_REF
    fprintf(stderr, "pool_unref('%s')=%u\n", pool->name, pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, &pool->unrefs, file, line);
#endif

    if (pool->ref == 0) {
        if (pool->parent != NULL)
            pool_remove_child(pool->parent, pool);
#ifdef DUMP_POOL_UNREF
        pool_dump_refs(pool);
#endif
        pool_destroy(pool);
        return 0;
    }

    return pool->ref;
}

#ifndef NDEBUG

void
pool_commit(void)
{
    if (!list_empty(&trash)) {
        pool_t pool;

        fprintf(stderr, "pool_commit(): there are unreleased pools in the trash:");

        for (pool = (pool_t)trash.next; &pool->siblings != &trash;
             pool = (pool_t)pool->siblings.next) {
#ifdef DEBUG_POOL_REF
            pool_dump_refs(pool);
#else
            fprintf(stderr, " '%s'(%u)", pool->name, pool->ref);
#endif
        }
        fprintf(stderr, "\n");

        abort();
    }
}
#endif

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

    if (unlikely(area->used + size > area->size)) {
        size_t new_area_size = area->size;
        fprintf(stderr, "growing linear pool '%s'\n", pool->name);
        if (size > new_area_size)
            new_area_size = ((size + new_area_size - 1) / new_area_size) * new_area_size;
        area = pool_get_linear_area(area, new_area_size);
        pool->current_area.linear = area;
    }

    p = area->data + area->used;
    area->used += size;

#ifdef VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(p, size);
#endif

    return p;
}

static void *
internal_malloc(pool_t pool, size_t size)
{
    assert(pool != NULL);

    if (likely(pool->type == POOL_LINEAR))
        return p_malloc_linear(pool, size);

    assert(pool->type == POOL_LIBC);
    return p_malloc_libc(pool, size);
}

void *
p_malloc(pool_t pool, size_t size)
{
    return internal_malloc(pool, align_size(size));
}

static inline void
clear_memory(void *p, size_t size)
{
#if defined(__x86_64__)
    size_t n = (size + 7) / 8;
    size_t dummy0, dummy1;
    asm volatile("cld\n\t"
                 "rep stosq\n\t"
                 : "=&c"(dummy0), "=&D"(dummy1)
                 : "a"(0), "0"(n), "1"(p)
                   /* : "memory"   memory barrier not required here */
                 );
#else
    memset(p, 0, size);
#endif
}

void *
p_calloc(pool_t pool, size_t size)
{
    void *p = internal_malloc(pool, align_size(size));
    clear_memory(p, size);
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

char * attr_malloc
p_sprintf(pool_t pool, const char *fmt, ...)
{
#if __STDC_VERSION__ >= 199901L
    size_t length;
    int length2;
    va_list ap;
    char *p;

    va_start(ap, fmt);
    length = (size_t)vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    p = p_malloc(pool, length);

    va_start(ap, fmt);
    length2 = vsnprintf(p, length, fmt, ap);
    va_end(ap);

    assert((size_t)length2 + 1 == length);

    return p;
#else
#error C99 required for snprintf(NULL, 0, ...)
#endif
}

char * attr_malloc attr_printf(2, 3)
p_strcat(pool_t pool, const char *first, ...)
{
    size_t length = 1;
    va_list ap;
    const char *s;
    char *ret, *p;

    va_start(ap, first);
    for (s = first; s != NULL; s = va_arg(ap, const char*))
        length += strlen(s);
    va_end(ap);

    ret = p = p_malloc(pool, length);

    va_start(ap, first);
    for (s = first; s != NULL; s = va_arg(ap, const char*)) {
        length = strlen(s);
        memcpy(p, s, length);
        p += length;
    }
    va_end(ap);

    *p = 0;

    return ret;
}
