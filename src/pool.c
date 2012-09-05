/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.h"

#include <inline/poison.h>
#include <inline/list.h>
#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#if defined(DEBUG_POOL_GROW) || defined(DUMP_POOL_ALLOC_ALL)
#define DUMP_POOL_ALLOC
#endif

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

#ifndef NDEBUG
struct allocation_info {
    struct list_head siblings;
    size_t size;
    const char *file;
    unsigned line;
};

struct attachment {
    struct list_head siblings;

    const void *value;

    const char *name;
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
#ifdef POISON
    size_t size;
#endif
#ifndef NDEBUG
    struct allocation_info info;
#endif
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

#ifdef TRACE
    const char *file;
    unsigned line;
#endif

    unsigned count;
};
#endif

struct pool {
    struct list_head siblings, children;
#ifdef DEBUG_POOL_REF
    struct list_head refs, unrefs;
#endif
    struct pool *parent;
    unsigned ref;

#ifndef NDEBUG
    struct list_head notify;
    bool trashed;
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

#ifndef NDEBUG
    struct list_head allocations;
    struct list_head attachments;
#endif

    size_t size;
};

#ifndef NDEBUG
static LIST_HEAD(trash);
#endif

static struct {
    unsigned num_pools;
    struct pool *pools;
    unsigned num_linear_areas;
    struct linear_pool_area *linear_areas;
} recycler;

static void * gcc_malloc
xmalloc(size_t size)
{
    void *p = malloc(size);
    if (unlikely(p == NULL)) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

static inline size_t gcc_const
align_size(size_t size)
{
    return ((size - 1) | ALIGN_BITS) + 1;
}

#ifndef NDEBUG
static struct allocation_info *
get_linear_allocation_info(void *p)
{
    return (struct allocation_info *)((char*)p - sizeof(struct allocation_info));
}
#endif

void
pool_recycler_clear(void)
{
    struct pool *pool;
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
pool_recycler_put(struct pool *pool)
{
    poison_undefined(pool, sizeof(*pool));
    pool->current_area.recycler = recycler.pools;
    recycler.pools = pool;
    ++recycler.num_pools;
}

static void
pool_recycler_put_linear(struct linear_pool_area *area)
{
    assert(area != NULL);
    assert(area->size > 0);

    if (recycler.num_linear_areas < RECYCLER_MAX_LINEAR_AREAS &&
        area->size <= RECYCLER_MAX_LINEAR_SIZE) {
        poison_noaccess(area->data, area->used);

        area->prev = recycler.linear_areas;
        recycler.linear_areas = area;
        ++recycler.num_linear_areas;
    } else {
        poison_undefined(area->data, area->used);
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
pool_add_child(struct pool *pool, struct pool *child)
{
    assert(child->parent == NULL);

    child->parent = pool;
    list_add(&child->siblings, &pool->children);
}

static inline void
pool_remove_child(struct pool *pool, struct pool *child)
{
    (void)pool;

    assert(child->parent == pool);

    list_remove(&child->siblings);
    child->parent = NULL;
}

static struct pool *gcc_malloc
pool_new(struct pool *parent, const char *name)
{
    struct pool *pool;

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
    list_init(&pool->notify);
    pool->trashed = false;
#endif
    pool->name = name;
#ifndef NDEBUG
    pool->major = parent == NULL;
#endif

    pool->parent = NULL;
    if (parent != NULL)
        pool_add_child(parent, pool);

#ifndef NDEBUG
    list_init(&pool->allocations);
    list_init(&pool->attachments);
#endif

    pool->size = 0;

    return pool;
}

struct pool *
pool_new_libc(struct pool *parent, const char *name)
{
    struct pool *pool = pool_new(parent, name);
    pool->type = POOL_LIBC;
    list_init(&pool->current_area.libc);
    return pool;
}

static struct linear_pool_area * gcc_malloc
pool_new_linear_area(struct linear_pool_area *prev, size_t size)
{
    struct linear_pool_area *area = xmalloc(sizeof(*area) - sizeof(area->data) + size);
    if (area == NULL)
        abort();
    area->prev = prev;
    area->size = size;
    area->used = 0;

    poison_noaccess(area->data, area->size);

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

struct pool *
pool_new_linear(struct pool *parent, const char *name, size_t initial_size)
{
#ifdef POOL_LIBC_ONLY
    (void)initial_size;

    return pool_new_libc(parent, name);
#else
    struct pool *pool = pool_new(parent, name);
    pool->type = POOL_LINEAR;

    pool->current_area.linear = pool_get_linear_area(NULL, initial_size);

    assert(parent != NULL);

    return pool;
#endif
}

#ifndef NDEBUG
void
pool_set_major(struct pool *pool)
{
    assert(!pool->trashed);
    assert(list_empty(&pool->children));

    pool->major = 1;
}
#endif

#ifdef DUMP_POOL_ALLOC_ALL
static void
pool_dump_allocations(struct pool *pool);
#endif

static void
pool_check_attachments(struct pool *pool)
{
#ifdef NDEBUG
    (void)pool;
#else
    if (list_empty(&pool->attachments))
        return;

    daemon_log(1, "pool '%s' has attachments left:\n", pool->name);

    do {
        struct attachment *attachment =
            (struct attachment *)pool->attachments.next;
        list_remove(&attachment->siblings);
        daemon_log(1, "\tname='%s' value=%p\n",
                   attachment->name, attachment->value);
    } while (!list_empty(&pool->attachments));

    abort();
#endif
}

static void
pool_destroy(struct pool *pool, struct pool *reparent_to)
{
    assert(pool->ref == 0);
    assert(pool->parent == NULL);

#ifdef DUMP_POOL_SIZE
    daemon_log(4, "pool '%s' size=%zu\n", pool->name, pool->size);
#endif

#ifdef DUMP_POOL_ALLOC_ALL
    pool_dump_allocations(pool);
#endif

    pool_check_attachments(pool);

#ifndef NDEBUG
    while (!list_empty(&pool->notify)) {
        struct pool_notify *notify = (struct pool_notify *)pool->notify.next;
        list_remove(&notify->siblings);
        notify->destroyed = 1;
    }

    if (pool->trashed)
        list_remove(&pool->siblings);
#endif

    while (!list_empty(&pool->children)) {
        struct pool *child = (struct pool *)pool->children.next;
        pool_remove_child(pool, child);
        assert(child->ref > 0);

        if (reparent_to == NULL) {
            /* children of major pools are put on trash, so they are
               collected by pool_commit() */
            assert(pool->major || pool->trashed);

#ifndef NDEBUG
            list_add(&child->siblings, &trash);
            child->trashed = true;
#else
            child->parent = NULL;
#endif
        } else {
            /* reparent all children of the destroyed pool to its
               parent, so they can live on - this reparenting never
               traverses major pools */

            assert(!pool->major && !pool->trashed);

            pool_add_child(reparent_to, child);
        }
    }

#ifdef DEBUG_POOL_REF
    while (!list_empty(&pool->refs)) {
        struct list_head *next = pool->refs.next;
        list_remove(next);
        free(next);
    }

    while (!list_empty(&pool->unrefs)) {
        struct list_head *next = pool->unrefs.next;
        list_remove(next);
        free(next);
    }
#endif

    switch (pool->type) {
    case POOL_LIBC:
        while (!list_empty(&pool->current_area.libc)) {
            struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)pool->current_area.libc.next;
            list_remove(&chunk->siblings);
#ifdef POISON
            poison_undefined(chunk, sizeof(*chunk) - sizeof(chunk->data) + chunk->size);
#endif
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

    if (recycler.num_pools < RECYCLER_MAX_POOLS)
        pool_recycler_put(pool);
    else
        free(pool);
}

#ifdef DEBUG_POOL_REF
static void
pool_increment_ref(struct pool *pool, struct list_head *list TRACE_ARGS_DECL)
{
    struct pool_ref *ref;

    (void)pool;

    for (ref = (struct pool_ref *)list->next;
         &ref->list_head != list;
         ref = (struct pool_ref *)ref->list_head.next) {
        assert(ref->list_head.next->prev == &ref->list_head);
        assert(ref->list_head.prev->next == &ref->list_head);

#ifdef TRACE
        if (ref->line == line && strcmp(ref->file, file) == 0) {
            ++ref->count;
            return;
        }
#endif
    }

    ref = xmalloc(sizeof(*ref));

#ifdef TRACE
    ref->file = file;
    ref->line = line;
#endif

    ref->count = 1;
    list_add(&ref->list_head, list);
}
#endif

#ifdef DEBUG_POOL_REF
static void
pool_dump_refs(struct pool *pool)
{
    daemon_log(0, "pool '%s'[%p](%u) REF:\n", pool->name,
               (const void*)pool, pool->ref);

#ifdef TRACE
    const struct pool_ref *ref;
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
#endif
}
#endif

void
pool_ref_impl(struct pool *pool TRACE_ARGS_DECL)
{
    assert(pool->ref > 0);
    ++pool->ref;

#ifdef POOL_TRACE_REF
    daemon_log(0, "pool_ref('%s')=%u\n", pool->name, pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, &pool->refs TRACE_ARGS_FWD);
#endif
}

unsigned
pool_unref_impl(struct pool *pool TRACE_ARGS_DECL)
{
    assert(pool->ref > 0);
    --pool->ref;

#ifdef POOL_TRACE_REF
    daemon_log(0, "pool_unref('%s')=%u\n", pool->name, pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, &pool->unrefs TRACE_ARGS_FWD);
#endif

    if (unlikely(pool->ref == 0)) {
#ifdef NDEBUG
        struct pool *reparent_to = NULL;
#else
        struct pool *reparent_to = pool->major ? NULL : pool->parent;
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

size_t
pool_size(const struct pool *pool)
{
    return pool->size;
}

size_t
pool_recursive_size(const struct pool *pool)
{
    return pool_size(pool) + pool_children_size(pool);
}

size_t
pool_children_size(const struct pool *pool)
{
    size_t size = 0;

    for (const struct pool *child = (const struct pool *)pool->children.next;
         &child->siblings != &pool->children;
         child = (const struct pool *)child->siblings.next)
        size += pool_recursive_size(child);

    return size;
}

static const char *
pool_type_string(enum pool_type type)
{
    switch (type) {
    case POOL_LIBC:
        return "libc";

    case POOL_LINEAR:
        return "linear";
    }

    assert(false);
    return NULL;
}

static void
pool_dump_node(int indent, const struct pool *pool)
{
    daemon_log(2, "%*spool '%s' type=%s ref=%u size=%zu p=%p\n",
               indent, "",
               pool->name, pool_type_string(pool->type),
               pool->ref, pool->size,
               (const void *)pool);

    indent += 2;
    for (struct pool *child = (struct pool *)pool->children.next;
         &child->siblings != &pool->children;
         child = (struct pool *)child->siblings.next)
        pool_dump_node(indent, child);
}

void
pool_dump_tree(const struct pool *pool)
{
    pool_dump_node(0, pool);
}

#ifndef NDEBUG

void
pool_notify(struct pool *pool, struct pool_notify *notify)
{
    list_add(&notify->siblings, &pool->notify);
    notify->pool = pool;
    notify->registered = true;
    notify->destroyed = 0;
}

bool
pool_denotify(struct pool_notify *notify)
{
    assert(notify->registered);
    notify->registered = false;

    if (notify->destroyed)
        return true;
    list_remove(&notify->siblings);
    return false;
}

void
pool_notify_move(struct pool *pool, struct pool_notify *src,
                 struct pool_notify *dest)
{
    assert(src->pool == pool);

#ifdef TRACE
    dest->file = src->file;
    dest->line = src->line;
#endif

    assert(!pool_denotify(src));
    pool_notify(pool, dest);
}

void
pool_ref_notify_impl(struct pool *pool, struct pool_notify *notify TRACE_ARGS_DECL)
{
    pool_notify(pool, notify);
    pool_ref_impl(pool TRACE_ARGS_FWD);

#ifdef TRACE
    notify->file = NULL;
    notify->line = -1;
#endif
}

void
pool_unref_denotify_impl(struct pool *pool, struct pool_notify *notify
                         TRACE_ARGS_DECL)
{
    assert(notify->pool == pool);
    assert(!notify->destroyed);
#ifdef TRACE
    assert(notify->file == NULL);
    assert(notify->line == -1);
#endif

    pool_denotify(notify);
    pool_unref_impl(pool TRACE_ARGS_FWD);

#ifdef TRACE
    notify->file = file;
    notify->line = line;
#endif
}

void
pool_trash(struct pool *pool)
{
    if (pool->trashed)
        return;

    assert(pool->parent != NULL);

    pool_remove_child(pool->parent, pool);
    list_add(&pool->siblings, &trash);
    pool->trashed = true;
}

void
pool_commit(void)
{
    struct pool *pool;

    if (list_empty(&trash))
        return;

    daemon_log(0, "pool_commit(): there are unreleased pools in the trash:\n");

    for (pool = (struct pool *)trash.next; &pool->siblings != &trash;
         pool = (struct pool *)pool->siblings.next) {
#ifdef DEBUG_POOL_REF
        pool_dump_refs(pool);
#else
        daemon_log(0, "- '%s'(%u)\n", pool->name, pool->ref);
#endif
    }
    daemon_log(0, "\n");

    abort();
}

static bool
linear_pool_area_contains(const struct linear_pool_area *area,
                          const void *ptr, size_t size)
{
    return size <= area->used &&
        ptr >= (const void*)area->data &&
        ptr <= (const void*)(area->data + area->used - size);
}

bool
pool_contains(struct pool *pool, const void *ptr, size_t size)
{
    const struct linear_pool_area *area;

    assert(pool != NULL);
    assert(ptr != NULL);
    assert(size > 0);

    if (pool->type != POOL_LINEAR)
        return true;

    for (area = pool->current_area.linear; area != NULL; area = area->prev)
        if (linear_pool_area_contains(area, ptr, size))
            return true;

    return false;
}

#endif

void
pool_mark(struct pool *pool, struct pool_mark *mark)
{
#ifndef POOL_LIBC_ONLY
    assert(pool->type == POOL_LINEAR);

    mark->area = pool->current_area.linear;
    mark->position = mark->area->used;
#else
    (void)pool;
    (void)mark;
#endif
}

#ifndef POOL_LIBC_ONLY
static void
pool_remove_allocations(struct pool *pool, const unsigned char *p, size_t length)
{
#ifndef NDEBUG
    struct allocation_info *info =
        (struct allocation_info *)pool->allocations.next;

    while (info != (struct allocation_info *)&pool->allocations) {
        struct allocation_info *next
            = (struct allocation_info *)info->siblings.next;
        if ((const unsigned char*)info >= p &&
            (const unsigned char*)(info + 1) + info->size <= p + length)
            list_remove(&info->siblings);
        info = next;
    }
#else
    (void)pool;
    (void)p;
    (void)length;
#endif
}
#endif

void
pool_rewind(struct pool *pool, const struct pool_mark *mark)
{
#ifndef POOL_LIBC_ONLY
    assert(pool->type == POOL_LINEAR);
    assert(mark->area != NULL);
    assert(mark->position <= mark->area->used);

    while (pool->current_area.linear != mark->area) {
        struct linear_pool_area *area = pool->current_area.linear;
        assert(area != NULL);

        pool_remove_allocations(pool, area->data, area->used);

        pool->current_area.linear = area->prev;
        pool_recycler_put_linear(area);
    }

    pool_remove_allocations(pool, mark->area->data + mark->position,
                            mark->area->used - mark->position);

    poison_noaccess(mark->area->data + mark->position,
                    mark->area->used - mark->position);

    mark->area->used = mark->position;
#else
    (void)pool;
    (void)mark;
#endif
}

static void *
p_malloc_libc(struct pool *pool, size_t size TRACE_ARGS_DECL)
{
    const size_t aligned_size = align_size(size);
    struct libc_pool_chunk *chunk =
        xmalloc(sizeof(*chunk) - sizeof(chunk->data) + aligned_size);

#ifndef NDEBUG
    list_add(&chunk->info.siblings, &pool->allocations);
    chunk->info.file = file;
    chunk->info.line = line;
    chunk->info.size = size;
#endif

    list_add(&chunk->siblings, &pool->current_area.libc);
#ifdef POISON
    chunk->size = size;
#endif
    return chunk->data;
}

#ifdef DUMP_POOL_ALLOC
static void
pool_dump_allocations(struct pool *pool)
{
    struct allocation_info *info;
    size_t sum;

    sum = 0;
    for (info = (struct allocation_info *)pool->allocations.prev;
         info != (struct allocation_info *)&pool->allocations;
         info = (struct allocation_info *)info->siblings.prev) {
        sum += info->size;
        daemon_log(6, "- %s:%u %zu => %zu\n", info->file, info->line, info->size, sum);
    }
}
#endif

static void *
p_malloc_linear(struct pool *pool, const size_t original_size
                TRACE_ARGS_DECL)
{
    struct linear_pool_area *area = pool->current_area.linear;
    void *p;
#ifndef NDEBUG
    struct allocation_info *info;
#endif

    assert(area != NULL);

    size_t size = align_size(original_size);
    size += LINEAR_PREFIX;

    if (unlikely(area->used + size > area->size)) {
        size_t new_area_size = area->size;
        daemon_log(5, "growing linear pool '%s'\n", pool->name);
#ifdef DEBUG_POOL_GROW
        pool_dump_allocations(pool);
        daemon_log(6, "+ %s:%u %zu\n", file, line, size - LINEAR_PREFIX);
#else
        TRACE_ARGS_IGNORE;
#endif
        if (size > new_area_size)
            new_area_size = ((size + new_area_size - 1) / new_area_size) * new_area_size;
        area = pool_get_linear_area(area, new_area_size);
        pool->current_area.linear = area;
    }

    p = area->data + area->used;
    area->used += size;

    poison_undefined(p, size);

#ifndef NDEBUG
    info = p;
    info->file = file;
    info->line = line;
    info->size = original_size;
    list_add(&info->siblings, &pool->allocations);
#endif

    return (char*)p + LINEAR_PREFIX;
}

static void *
internal_malloc(struct pool *pool, size_t size TRACE_ARGS_DECL)
{
    assert(pool != NULL);

    pool->size += size;

    if (likely(pool->type == POOL_LINEAR))
        return p_malloc_linear(pool, size TRACE_ARGS_FWD);

    assert(pool->type == POOL_LIBC);
    return p_malloc_libc(pool, size TRACE_ARGS_FWD);
}

void *
p_malloc_impl(struct pool *pool, size_t size TRACE_ARGS_DECL)
{
    return internal_malloc(pool, size TRACE_ARGS_FWD);
}

static void
p_free_libc(struct pool *pool, void *ptr)
{
    struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)(((char*)ptr) -
                                                               offsetof(struct libc_pool_chunk, data));

    (void)pool;

#ifndef NDEBUG
    list_remove(&chunk->info.siblings);
#endif

    list_remove(&chunk->siblings);
    free(chunk);
}

void
p_free(struct pool *pool, const void *cptr)
{
    /* deconst hack - we know what we're doing![tm] */
    union {
        const void *in;
        void *out;
    } u = { .in = cptr };
    void *ptr = u.out;

    assert(pool != NULL);
    assert(ptr != NULL);
    assert((((unsigned long)ptr) & ALIGN_BITS) == 0);
    assert(pool_contains(pool, ptr, 1));

    if (pool->type == POOL_LIBC)
        p_free_libc(pool, ptr);
#ifndef NDEBUG
    else if (pool->type == POOL_LINEAR) {
        struct allocation_info *info = get_linear_allocation_info(ptr);
        list_remove(&info->siblings);
        poison_noaccess(ptr, info->size);
    }
#endif
    else
        /* we don't know the exact size of this buffer, so we only
           mark the first ALIGN bytes */
        poison_noaccess(ptr, ALIGN);
}

static inline void
clear_memory(void *p, size_t size)
{
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wlanguage-extension-token"
#endif
#if defined(__GNUC__) && defined(__x86_64__)
    size_t n = (size + 7) / 8;
    size_t gcc_unused dummy0, dummy1;
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
p_calloc_impl(struct pool *pool, size_t size TRACE_ARGS_DECL)
{
    void *p = internal_malloc(pool, size TRACE_ARGS_FWD);
    clear_memory(p, size);
    return p;
}

#ifndef NDEBUG

void
pool_attach(struct pool *pool, const void *p, const char *name)
{
    assert(pool != NULL);
    assert(p != NULL);
    assert(name != NULL);

    struct attachment *attachment = p_malloc(pool, sizeof(*attachment));
    attachment->value = p;
    attachment->name = name;

    list_add(&attachment->siblings, &pool->attachments);
}

static struct attachment *
find_attachment(struct pool *pool, const void *p)
{
    for (struct attachment *attachment = (struct attachment *)pool->attachments.next;
         &attachment->siblings != &pool->attachments;
         attachment = (struct attachment *)attachment->siblings.next)
        if (attachment->value == p)
            return attachment;

    return NULL;
}

void
pool_attach_checked(struct pool *pool, const void *p, const char *name)
{
    assert(pool != NULL);
    assert(p != NULL);
    assert(name != NULL);

    if (find_attachment(pool, p) != NULL)
        return;

    pool_attach(pool, p, name);
}

void
pool_detach(struct pool *pool, const void *p)
{
    struct attachment *attachment = find_attachment(pool, p);
    assert(attachment != NULL);

    list_remove(&attachment->siblings);
}

void
pool_detach_checked(struct pool *pool, const void *p)
{
    struct attachment *attachment = find_attachment(pool, p);
    if (attachment == NULL)
        return;

    list_remove(&attachment->siblings);
}

const char *
pool_attachment_name(struct pool *pool, const void *p)
{
    struct attachment *attachment = find_attachment(pool, p);
    return attachment != NULL
        ? attachment->name
        : NULL;
}

#endif
