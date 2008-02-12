/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.h"
#include "list.h"
#include "compiler.h"
#include "valgrind.h"

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

#ifdef DEBUG_POOL_GROW
struct allocation_info {
    struct list_head siblings;
    const char *file;
    unsigned line;
    size_t size;
};
#define LINEAR_PREFIX sizeof(struct allocation_info)
#else
#define LINEAR_PREFIX 0
#endif

enum pool_type {
    POOL_LIBC,
    POOL_LINEAR,
};

struct libc_pool_chunk {
    struct list_head siblings;
    unsigned char data[sizeof(size_t)];
};

struct linear_pool_area {
    struct linear_pool_area *prev;
    size_t size, used;
    unsigned char data[sizeof(size_t)];
};

#ifdef DEBUG_POOL_REF
struct pool_ref {
    struct list_head list_head;
    const char *file;
    unsigned line;
    unsigned count;
};
#endif

struct pool {
    struct list_head siblings, children;
#ifdef DEBUG_POOL_REF
    struct list_head refs, unrefs;
    struct pool_ref main_ref;
#endif
    pool_t parent;
    unsigned ref;

#ifndef NDEBUG
    int trashed;
#endif

    enum pool_type type;
    const char *name;

#ifndef NDEBUG
    /** this is a major pool, i.e. pool commits are performed after
        the major pool is freed */
    int major;
#endif

    union {
        struct list_head libc;
        struct linear_pool_area *linear;
        struct pool *recycler;
    } current_area;

#ifdef DEBUG_POOL_GROW
    struct list_head allocations;
#endif
#ifdef DUMP_POOL_SIZE
    size_t size;
#endif
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
    assert(area != NULL);
    assert(area->size > 0);

#ifdef POISON
    memset(area->data, 0x01, area->used);
#endif

    if (recycler.num_linear_areas < RECYCLER_MAX_LINEAR_AREAS &&
        area->size <= RECYCLER_MAX_LINEAR_SIZE) {
        VALGRIND_MAKE_MEM_NOACCESS(area->data, area->size);

        area->prev = recycler.linear_areas;
        recycler.linear_areas = area;
        ++recycler.num_linear_areas;
    } else {
        VALGRIND_MAKE_MEM_UNDEFINED(area, sizeof(*area) - sizeof(area->data) + area->size);

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
#ifndef NDEBUG
    pool->trashed = 0;
#endif
    pool->name = name;
#ifndef NDEBUG
    pool->major = parent == NULL;
#endif

    pool->parent = NULL;
    if (parent != NULL)
        pool_add_child(parent, pool);

#ifdef DEBUG_POOL_GROW
    list_init(&pool->allocations);
#endif
#ifdef DUMP_POOL_SIZE
    pool->size = 0;
#endif

    return pool;
}

pool_t
pool_new_libc(pool_t parent, const char *name)
{
    pool_t pool = pool_new(parent, name);
    pool->type = POOL_LIBC;
    list_init(&pool->current_area.libc);
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

    VALGRIND_MAKE_MEM_NOACCESS(area->data, area->size);

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
#ifdef POOL_LIBC_ONLY
    (void)initial_size;

    return pool_new_libc(parent, name);
#else
    pool_t pool = pool_new(parent, name);
    pool->type = POOL_LINEAR;

    pool->current_area.linear = pool_get_linear_area(NULL, initial_size);

    assert(parent != NULL);

    return pool;
#endif
}

#ifndef NDEBUG
void
pool_set_major(pool_t pool)
{
    assert(!pool->trashed);
    assert(list_empty(&pool->children));

    pool->major = 1;
}
#endif

static void
pool_destroy(pool_t pool, pool_t reparent_to)
{
    assert(pool->ref == 0);
    assert(pool->parent == NULL);

    (void)reparent_to;

#ifdef DUMP_POOL_SIZE
    daemon_log(4, "pool '%s' size=%zu\n", pool->name, pool->size);
#endif

#ifndef NDEBUG
    if (pool->trashed)
        list_remove(&pool->siblings);

    while (!list_empty(&pool->children)) {
        pool_t child = (pool_t)pool->children.next;
        pool_remove_child(pool, child);
        assert(child->ref > 0);

        if (reparent_to == NULL) {
            /* children of major pools are put on trash, so they are
               collected by pool_commit() */
            assert(pool->major || pool->trashed);

            list_add(&child->siblings, &trash);
            child->trashed = 1;
        } else {
            /* reparent all children of the destroyed pool to its
               parent, so they can live on - this reparenting never
               traverses major pools */

            assert(!pool->major && !pool->trashed);

            pool_add_child(reparent_to, child);
        }
    }
#endif

    switch (pool->type) {
    case POOL_LIBC:
        while (!list_empty(&pool->current_area.libc)) {
            struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)pool->current_area.libc.next;
            list_remove(&chunk->siblings);
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

    for (ref = (struct pool_ref *)list->next;
         &ref->list_head != list;
         ref = (struct pool_ref *)ref->list_head.next) {
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
    daemon_log(0, "pool '%s'(%u) REF:\n", pool->name, pool->ref);
    for (ref = (const struct pool_ref *)pool->refs.next;
         &ref->list_head != &pool->refs;
         ref = (const struct pool_ref *)ref->list_head.next) {
        daemon_log(0, "\t%s:%u %u\n", ref->file, ref->line, ref->count);
    }
    daemon_log(0, "    UNREF:\n");
    for (ref = (const struct pool_ref *)pool->unrefs.next;
         &ref->list_head != &pool->unrefs;
         ref = (const struct pool_ref *)ref->list_head.next) {
        daemon_log(0, "\t%s:%u %u\n", ref->file, ref->line, ref->count);
    }
}
#endif

void
pool_ref_impl(pool_t pool TRACE_ARGS_DECL)
{
    assert(pool->ref > 0);
    ++pool->ref;

#ifdef POOL_TRACE_REF
    daemon_log(0, "pool_ref('%s')=%u\n", pool->name, pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, &pool->refs, file, line);
#endif
}

unsigned
pool_unref_impl(pool_t pool TRACE_ARGS_DECL)
{
    assert(pool->ref > 0);
    --pool->ref;

#ifdef POOL_TRACE_REF
    daemon_log(0, "pool_unref('%s')=%u\n", pool->name, pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, &pool->unrefs, file, line);
#endif

    if (pool->ref == 0) {
#ifdef NDEBUG
        pool_t reparent_to = NULL;
#else
        pool_t reparent_to = pool->major ? NULL : pool->parent;
#endif
        if (pool->parent != NULL)
            pool_remove_child(pool->parent, pool);
#ifdef DUMP_POOL_UNREF
        pool_dump_refs(pool);
#endif
        pool_destroy(pool, reparent_to);
        return 0;
    }

    return pool->ref;
}

#ifndef NDEBUG

void
pool_commit(void)
{
    pool_t pool;

    if (list_empty(&trash))
        return;

    daemon_log(0, "pool_commit(): there are unreleased pools in the trash:\n");

    for (pool = (pool_t)trash.next; &pool->siblings != &trash;
         pool = (pool_t)pool->siblings.next) {
#ifdef DEBUG_POOL_REF
        pool_dump_refs(pool);
#else
        daemon_log(0, "- '%s'(%u)\n", pool->name, pool->ref);
#endif
    }
    daemon_log(0, "\n");

    abort();
}

static int
linear_pool_area_contains(const struct linear_pool_area *area,
                          const void *ptr, size_t size)
{
    return size <= area->used &&
        ptr >= (const void*)area->data &&
        ptr <= (const void*)(area->data + area->used - size);
}

int
pool_contains(pool_t pool, const void *ptr, size_t size)
{
    const struct linear_pool_area *area;

    assert(pool != NULL);
    assert(ptr != NULL);
    assert(size > 0);

    if (pool->type != POOL_LINEAR)
        return 1;

    for (area = pool->current_area.linear; area != NULL; area = area->prev)
        if (linear_pool_area_contains(area, ptr, size))
            return 1;

    return 0;
}

#endif

static void *
p_malloc_libc(pool_t pool, size_t size)
{
    struct libc_pool_chunk *chunk = xmalloc(sizeof(*chunk) - sizeof(chunk->data) + size);
    list_add(&chunk->siblings, &pool->current_area.libc);
    return chunk->data;
}

static void *
p_malloc_linear(pool_t pool, size_t size TRACE_ARGS_DECL)
{
    struct linear_pool_area *area = pool->current_area.linear;
    void *p;
#ifdef DEBUG_POOL_GROW
    struct allocation_info *info;
    size_t sum;
#endif

    size += LINEAR_PREFIX;

    if (unlikely(area->used + size > area->size)) {
        size_t new_area_size = area->size;
        daemon_log(5, "growing linear pool '%s'\n", pool->name);
#ifdef DEBUG_POOL_GROW
        sum = 0;
        for (info = (struct allocation_info *)pool->allocations.prev;
             info != (struct allocation_info *)&pool->allocations;
             info = (struct allocation_info *)info->siblings.prev) {
            sum += info->size;
            daemon_log(6, "- %s:%u %zu => %zu\n", info->file, info->line, info->size, sum);
        }
        daemon_log(6, "+ %s:%u %zu => %zu\n", file, line, size - LINEAR_PREFIX, sum + size - LINEAR_PREFIX);
#endif
        if (size > new_area_size)
            new_area_size = ((size + new_area_size - 1) / new_area_size) * new_area_size;
        area = pool_get_linear_area(area, new_area_size);
        pool->current_area.linear = area;
    }

    p = area->data + area->used;
    area->used += size;

    VALGRIND_MAKE_MEM_UNDEFINED(p, size);

#ifdef DEBUG_POOL_GROW
    info = p;
    info->file = file;
    info->line = line;
    info->size = size - LINEAR_PREFIX;
    list_add(&info->siblings, &pool->allocations);
#endif

    return (char*)p + LINEAR_PREFIX;
}

static void *
internal_malloc(pool_t pool, size_t size TRACE_ARGS_DECL)
{
    assert(pool != NULL);

#ifdef DUMP_POOL_SIZE
    pool->size += size;
#endif

    if (likely(pool->type == POOL_LINEAR))
        return p_malloc_linear(pool, size TRACE_ARGS_FWD);

    assert(pool->type == POOL_LIBC);
    return p_malloc_libc(pool, size);
}

void *
p_malloc_impl(pool_t pool, size_t size TRACE_ARGS_DECL)
{
    return internal_malloc(pool, align_size(size) TRACE_ARGS_FWD);
}

static void
p_free_libc(pool_t pool, void *ptr)
{
    struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)(((char*)ptr) -
                                                               offsetof(struct libc_pool_chunk, data));

    (void)pool;

    list_remove(&chunk->siblings);
    free(chunk);
}

void
p_free(pool_t pool, void *ptr)
{
    assert(pool != NULL);
    assert(ptr != NULL);
    assert(pool_contains(pool, ptr, 1));

    if (pool->type == POOL_LIBC)
        p_free_libc(pool, ptr);
    else
        /* we don't know the exact size of this buffer, so we only
           mark the first ALIGN bytes */
        VALGRIND_MAKE_MEM_NOACCESS(ptr, ALIGN);
}

static inline void
clear_memory(void *p, size_t size)
{
#if defined(__x86_64__)
    size_t n = (size + 7) / 8;
    size_t attr_unused dummy0, dummy1;
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
p_calloc_impl(pool_t pool, size_t size TRACE_ARGS_DECL)
{
    void *p = internal_malloc(pool, align_size(size) TRACE_ARGS_FWD);
    clear_memory(p, size);
    return p;
}
