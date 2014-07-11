/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pool.h"
#include "slice.hxx"

#include <inline/poison.h>
#include <inline/list.h>
#include <daemon/log.h>

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#endif

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

#ifdef POISON
static const size_t LIBC_POOL_CHUNK_HEADER =
    offsetof(struct libc_pool_chunk, data);
#endif

struct linear_pool_area {
    struct linear_pool_area *prev;

    /**
     * The slice_area that was used to allocated this pool area.  It
     * is nullptr if this area was allocated from the libc heap.
     */
    struct slice_area *slice_area;

    size_t size, used;
    unsigned char data[sizeof(size_t)];
};

static const size_t LINEAR_POOL_AREA_HEADER =
    offsetof(struct linear_pool_area, data);

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

    /** this is a major pool, i.e. pool commits are performed after
        the major pool is freed */
    bool major;

    /**
     * Does the pool survive the destruction of the parent pool?  It
     * will be reparented across destroyed "major" pools.  This flag
     * is only relevant in the debug build, because it disables the
     * memory leak checks.
     */
    bool persistent;
#endif

    enum pool_type type;
    const char *name;

    union {
        struct list_head libc;
        struct linear_pool_area *linear;
        struct pool *recycler;
    } current_area;

#ifndef NDEBUG
    struct list_head allocations;
    struct list_head attachments;
#endif

    struct slice_pool *slice_pool;

    /**
     * The area size passed to pool_new_linear().
     */
    size_t area_size;

    /**
     * The number of bytes allocated from this pool, not counting
     * overhead and not counting p_free().
     */
    size_t netto_size;
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
    if (unlikely(p == nullptr)) {
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
    void *q = (char *)p - sizeof(struct allocation_info);
    return (struct allocation_info *)q;
}
#endif

void
pool_recycler_clear(void)
{
    while (recycler.pools != nullptr) {
        struct pool *pool = recycler.pools;
        recycler.pools = pool->current_area.recycler;
        free(pool);
    }

    recycler.num_pools = 0;

    while (recycler.linear_areas != nullptr) {
        struct linear_pool_area *linear = recycler.linear_areas;
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

/**
 * @return true if the area was moved to the recycler, false if the
 * caller is responsible for freeing it
 */
static bool
pool_recycler_put_linear(struct linear_pool_area *area)
{
    assert(area != nullptr);
    assert(area->size > 0);
    assert(area->slice_area == nullptr);

    if (recycler.num_linear_areas >= RECYCLER_MAX_LINEAR_AREAS)
        return false;

    poison_noaccess(area->data, area->used);

    area->prev = recycler.linear_areas;
    recycler.linear_areas = area;
    ++recycler.num_linear_areas;
    return true;
}

static struct linear_pool_area *
pool_recycler_get_linear(size_t size)
{
    assert(size > 0);

    struct linear_pool_area **linear_p, *linear;
    for (linear_p = &recycler.linear_areas, linear = *linear_p;
         linear != nullptr;
         linear_p = &linear->prev, linear = *linear_p) {
        if (linear->size == size) {
            assert(recycler.num_linear_areas > 0);
            --recycler.num_linear_areas;
            *linear_p = linear->prev;
            return linear;
        }
    }

    return nullptr;
}

static void
pool_free_linear_area(struct linear_pool_area *area)
{
    assert(area->slice_area == nullptr);

    poison_undefined(area->data, area->used);
    free(area);
}

static bool
pool_dispose_slice_area(struct slice_pool *slice_pool,
                        struct linear_pool_area *area)
{
    if (area->slice_area == nullptr)
        return false;

    assert(slice_pool != nullptr);

    slice_free(slice_pool, area->slice_area, area);
    return true;
}

static void
pool_dispose_linear_area(struct pool *pool, struct linear_pool_area *area)
{
    if (/* recycle only if the area's size is exactly as big as
           planned, and was not superseded by a larger allocation;
           this avoids poisoning the recycler with areas that will
           probably never be used again */
        area->size != pool->area_size ||
        (!pool_dispose_slice_area(pool->slice_pool, area) &&
         !pool_recycler_put_linear(area)))
        pool_free_linear_area(area);
}

static inline void
pool_add_child(struct pool *pool, struct pool *child)
{
    assert(child->parent == nullptr);

    child->parent = pool;
    list_add(&child->siblings, &pool->children);
}

static inline void
pool_remove_child(gcc_unused struct pool *pool, struct pool *child)
{
    assert(child->parent == pool);

    list_remove(&child->siblings);
    child->parent = nullptr;
}

static struct pool *gcc_malloc
pool_new(struct pool *parent, const char *name)
{
    struct pool *pool;

    if (recycler.pools == nullptr)
        pool = (struct pool *)xmalloc(sizeof(*pool));
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
    pool->major = parent == nullptr;
    pool->persistent = false;
#endif

    pool->parent = nullptr;
    if (parent != nullptr)
        pool_add_child(parent, pool);

#ifndef NDEBUG
    list_init(&pool->allocations);
    list_init(&pool->attachments);
#endif

    pool->netto_size = 0;

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

gcc_malloc
static struct linear_pool_area *
pool_new_slice_area(struct slice_pool *slice_pool,
                    struct linear_pool_area *prev)
{
    struct slice_area *slice_area = slice_pool_get_area(slice_pool);
    assert(slice_area != nullptr);

    struct linear_pool_area *area = (struct linear_pool_area *)
        slice_alloc(slice_pool, slice_area);
    assert(area != nullptr);

    area->prev = prev;
    area->slice_area = slice_area;
    area->size = slice_pool_get_slice_size(slice_pool)
        - LINEAR_POOL_AREA_HEADER;
    area->used = 0;

    poison_noaccess(area->data, area->size);

    return area;
}

static struct linear_pool_area * gcc_malloc
pool_new_linear_area(struct linear_pool_area *prev, size_t size)
{
    struct linear_pool_area *area = (struct linear_pool_area *)
        xmalloc(LINEAR_POOL_AREA_HEADER + size);
    if (area == nullptr)
        abort();

    area->slice_area = nullptr;
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
    if (area == nullptr) {
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

#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        /* Valgrind cannot verify allocations and memory accesses with
           this library; therefore use the "libc" pool when running on
           valgrind */
        return pool_new_libc(parent, name);
#endif

    struct pool *pool = pool_new(parent, name);
    pool->type = POOL_LINEAR;
    pool->area_size = initial_size;
    pool->slice_pool = nullptr;
    pool->current_area.linear = nullptr;

    assert(parent != nullptr);

    return pool;
#endif
}

struct pool *
pool_new_slice(struct pool *parent, const char *name,
               struct slice_pool *slice_pool)
{
    assert(parent != nullptr);
    assert(slice_pool_get_slice_size(slice_pool) > LINEAR_POOL_AREA_HEADER);

#ifdef POOL_LIBC_ONLY
    (void)slice_pool;

    return pool_new_libc(parent, name);
#else

#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        /* Valgrind cannot verify allocations and memory accesses with
           this library; therefore use the "libc" pool when running on
           valgrind */
        return pool_new_libc(parent, name);
#endif

    struct pool *pool = pool_new(parent, name);
    pool->type = POOL_LINEAR;
    pool->area_size = slice_pool_get_slice_size(slice_pool) - LINEAR_POOL_AREA_HEADER;
    pool->slice_pool = slice_pool;
    pool->current_area.linear = nullptr;

    return pool;
#endif
}

#ifndef NDEBUG

#ifndef POOL_LIBC_ONLY

static bool
pool_linear_is_empty(const struct pool *pool)
{
    assert(pool->type == POOL_LINEAR);

    const struct linear_pool_area *area = pool->current_area.linear;
    return area == nullptr || (area->prev == nullptr && area->used == 0);
}

#endif

void
pool_set_major(struct pool *pool)
{
    assert(!pool->trashed);
    assert(list_empty(&pool->children));
    assert(!pool->persistent);

    pool->major = true;
}

void
pool_set_persistent(struct pool *pool)
{
    assert(!pool->trashed);
    assert(list_empty(&pool->children));
    assert(!pool->persistent);

    pool->major = true;
    pool->persistent = true;
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
pool_destroy(struct pool *pool, gcc_unused struct pool *parent,
             struct pool *reparent_to TRACE_ARGS_DECL)
{
    assert(pool->ref == 0);
    assert(pool->parent == nullptr);

#ifdef DUMP_POOL_SIZE
    daemon_log(4, "pool '%s' size=%zu\n", pool->name, pool->netto_size);
#endif

#ifdef DUMP_POOL_ALLOC_ALL
    pool_dump_allocations(pool);
#endif

    pool_check_attachments(pool);

#ifndef NDEBUG
    while (!list_empty(&pool->notify)) {
        struct pool_notify_state *notify =
            (struct pool_notify_state *)pool->notify.next;
        list_remove(&notify->siblings);
        notify->destroyed = 1;

#ifdef TRACE
        notify->destroyed_file = file;
        notify->destroyed_line = line;
#endif
    }

    if (pool->trashed)
        list_remove(&pool->siblings);
#endif

    while (!list_empty(&pool->children)) {
        struct pool *child = (struct pool *)pool->children.next;
        pool_remove_child(pool, child);
        assert(child->ref > 0);

        if (reparent_to == nullptr) {
            /* children of major pools are put on trash, so they are
               collected by pool_commit() */
            assert(pool->major || pool->trashed);

#ifndef NDEBUG
            if (child->persistent) {
                assert(child->major);

                if (parent != nullptr)
                    pool_add_child(parent, child);
                else
                    child->parent = nullptr;
            } else {
                list_add(&child->siblings, &trash);
                child->trashed = true;
            }
#else
            child->parent = nullptr;
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

#ifndef NDEBUG
    while (!list_empty(&pool->attachments)) {
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
            poison_undefined(chunk, LIBC_POOL_CHUNK_HEADER + chunk->size);
#endif
            free(chunk);
        }
        break;

    case POOL_LINEAR:
        while (pool->current_area.linear != nullptr) {
            struct linear_pool_area *area = pool->current_area.linear;
            pool->current_area.linear = area->prev;
            pool_dispose_linear_area(pool, area);
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
pool_increment_ref(gcc_unused struct pool *pool,
                   struct list_head *list TRACE_ARGS_DECL)
{
    struct pool_ref *ref;

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

    ref = (struct pool_ref *)xmalloc(sizeof(*ref));

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
        struct pool *parent = pool->parent;
#ifdef NDEBUG
        struct pool *reparent_to = nullptr;
#else
        struct pool *reparent_to = pool->major ? nullptr : parent;
#endif
        if (parent != nullptr)
            pool_remove_child(parent, pool);
#ifdef DUMP_POOL_UNREF
        pool_dump_refs(pool);
#endif
        pool_destroy(pool, parent, reparent_to TRACE_ARGS_FWD);
        return 0;
    }

    return pool->ref;
}

size_t
pool_netto_size(const struct pool *pool)
{
    return pool->netto_size;
}

static size_t
pool_linear_brutto_size(const struct pool *pool)
{
    size_t size = 0;

    for (const struct linear_pool_area *area = pool->current_area.linear;
         area != nullptr; area = area->prev)
        size += area->size;

    return size;
}

size_t
pool_brutto_size(const struct pool *pool)
{
    switch (pool->type) {
    case POOL_LIBC:
        return pool_netto_size(pool);

    case POOL_LINEAR:
        return pool_linear_brutto_size(pool);
    }

    assert(false);
    return 0;
}

size_t
pool_recursive_netto_size(const struct pool *pool)
{
    return pool_netto_size(pool) + pool_children_netto_size(pool);
}

size_t
pool_recursive_brutto_size(const struct pool *pool)
{
    return pool_brutto_size(pool) + pool_children_brutto_size(pool);
}

size_t
pool_children_netto_size(const struct pool *pool)
{
    size_t size = 0;

    for (const struct pool *child = (const struct pool *)pool->children.next;
         &child->siblings != &pool->children;
         child = (const struct pool *)child->siblings.next)
        size += pool_recursive_netto_size(child);

    return size;
}

size_t
pool_children_brutto_size(const struct pool *pool)
{
    size_t size = 0;

    for (const struct pool *child = (const struct pool *)pool->children.next;
         &child->siblings != &pool->children;
         child = (const struct pool *)child->siblings.next)
        size += pool_recursive_brutto_size(child);

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
    return nullptr;
}

static void
pool_dump_node(int indent, const struct pool *pool)
{
    daemon_log(2, "%*spool '%s' type=%s ref=%u size=%zu p=%p\n",
               indent, "",
               pool->name, pool_type_string(pool->type),
               pool->ref, pool->netto_size,
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
pool_notify(struct pool *pool, struct pool_notify_state *notify)
{
    list_add(&notify->siblings, &pool->notify);
    notify->pool = pool;
    notify->name = pool->name;
    notify->registered = true;
    notify->destroyed = 0;
}

bool
pool_denotify(struct pool_notify_state *notify)
{
    assert(notify->registered);
    notify->registered = false;

    if (notify->destroyed)
        return true;
    list_remove(&notify->siblings);
    return false;
}

void
pool_notify_move(struct pool *pool, struct pool_notify_state *src,
                 struct pool_notify_state *dest)
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
pool_ref_notify_impl(struct pool *pool, struct pool_notify_state *notify TRACE_ARGS_DECL)
{
    pool_notify(pool, notify);
    pool_ref_impl(pool TRACE_ARGS_FWD);

#ifdef TRACE
    notify->file = nullptr;
    notify->line = -1;
#endif
}

void
pool_unref_denotify_impl(struct pool *pool, struct pool_notify_state *notify
                         TRACE_ARGS_DECL)
{
    assert(notify->pool == pool);
    assert(!notify->destroyed);
#ifdef TRACE
    assert(notify->file == nullptr);
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

    assert(pool->parent != nullptr);

#ifndef NDEBUG
    if (pool->persistent)
        return;
#endif

    pool_remove_child(pool->parent, pool);
    list_add(&pool->siblings, &trash);
    pool->trashed = true;
}

void
pool_commit(void)
{
    if (list_empty(&trash))
        return;

    daemon_log(0, "pool_commit(): there are unreleased pools in the trash:\n");

    for (struct pool *pool = (struct pool *)trash.next;
         &pool->siblings != &trash;
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
    assert(pool != nullptr);
    assert(ptr != nullptr);
    assert(size > 0);

    if (pool->type != POOL_LINEAR)
        return true;

    for (const struct linear_pool_area *area = pool->current_area.linear;
         area != nullptr; area = area->prev)
        if (linear_pool_area_contains(area, ptr, size))
            return true;

    return false;
}

#endif

void
pool_mark(struct pool *pool, struct pool_mark_state *mark)
{
#ifndef POOL_LIBC_ONLY
    assert(pool->type == POOL_LINEAR);

    mark->area = pool->current_area.linear;
    mark->prev = mark->area != nullptr ? mark->area->prev : nullptr;
    mark->position = mark->area != nullptr ? mark->area->used : 0;

#ifndef NDEBUG
    mark->was_empty = pool_linear_is_empty(pool);
#endif
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
pool_rewind(struct pool *pool, const struct pool_mark_state *mark)
{
#ifndef POOL_LIBC_ONLY
    assert(pool->type == POOL_LINEAR);
    assert(mark->area == nullptr || mark->position <= mark->area->used);
    assert(mark->area != nullptr || mark->position == 0);

    struct linear_pool_area *const marked_area = mark->area;

    /* dispose all areas newer than the marked one */
    while (pool->current_area.linear != marked_area) {
        struct linear_pool_area *area = pool->current_area.linear;
        assert(area != nullptr);

        pool_remove_allocations(pool, area->data, area->used);

        pool->current_area.linear = area->prev;
        pool_dispose_linear_area(pool, area);
    }

    if (marked_area != nullptr) {
        /* dispose all (large) areas that were inserted before the marked
           one */
        while (marked_area->prev != mark->prev) {
            struct linear_pool_area *area = marked_area->prev;
            assert(area != nullptr);
            /* only large areas get inserted before the current one */
            assert(area->size > pool->area_size);
            assert(area->used > pool->area_size);

            pool_remove_allocations(pool, area->data, area->used);

            marked_area->prev = area->prev;
            pool_dispose_linear_area(pool, area);
        }

        /* rewind the marked area */

        pool_remove_allocations(pool, marked_area->data + mark->position,
                                marked_area->used - mark->position);

        poison_noaccess(marked_area->data + mark->position,
                        marked_area->used - mark->position);

        marked_area->used = mark->position;
    }

    /* if the pool was empty before pool_mark(), it must be empty
       again after pool_rewind() */
#ifndef NDEBUG
    assert(mark->was_empty == pool_linear_is_empty(pool));
#endif

    /* if the pool is empty again, the allocation list must be empty,
       too */
    assert(!pool_linear_is_empty(pool) || list_empty(&pool->allocations));
#else
    (void)pool;
    (void)mark;
#endif
}

static void *
p_malloc_libc(struct pool *pool, size_t size TRACE_ARGS_DECL)
{
    const size_t aligned_size = align_size(size);
    struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)
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
    size_t sum = 0;
    for (struct allocation_info *info = (struct allocation_info *)pool->allocations.prev;
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

    size_t size = align_size(original_size);
    size += LINEAR_PREFIX;

    if (unlikely(size > pool->area_size)) {
        /* this allocation is larger than the standard area size;
           obtain a new area just for this allocation, and keep on
           using the last area */
        daemon_log(5, "big allocation on linear pool '%s' (%zu bytes)\n",
                   pool->name, original_size);
#ifdef DEBUG_POOL_GROW
        pool_dump_allocations(pool);
        daemon_log(6, "+ %s:%u %zu\n", file, line, original_size);
#else
        TRACE_ARGS_IGNORE;
#endif

        if (area == nullptr) {
            /* this is the first allocation, create the initial
               area */
            area = pool->current_area.linear =
                pool_new_linear_area(nullptr, size);
        } else {
            /* put the special large area after the current one */
            area = pool_new_linear_area(area->prev, size);
            pool->current_area.linear->prev = area;
        }
    } else if (unlikely(area == nullptr || area->used + size > area->size)) {
        if (area != nullptr) {
            daemon_log(5, "growing linear pool '%s'\n", pool->name);
#ifdef DEBUG_POOL_GROW
            pool_dump_allocations(pool);
            daemon_log(6, "+ %s:%u %zu\n", file, line, original_size);
#else
            TRACE_ARGS_IGNORE;
#endif
        }

        area = pool->slice_pool != nullptr
            ? pool_new_slice_area(pool->slice_pool, area)
            : pool_get_linear_area(area, pool->area_size);
        pool->current_area.linear = area;
    }

    void *p = area->data + area->used;
    area->used += size;

    assert(area->used <= area->size);

    poison_undefined(p, size);

#ifndef NDEBUG
    struct allocation_info *info = (struct allocation_info *)p;
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
    assert(pool != nullptr);

    pool->netto_size += size;

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
p_free_libc(gcc_unused struct pool *pool, void *ptr)
{
    void *q = (char *)ptr - offsetof(struct libc_pool_chunk, data);
    struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)q;

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

    assert(pool != nullptr);
    assert(ptr != nullptr);
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
    assert(pool != nullptr);
    assert(p != nullptr);
    assert(name != nullptr);

    struct attachment *attachment = (struct attachment *)
        xmalloc(sizeof(*attachment));
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

    return nullptr;
}

void
pool_attach_checked(struct pool *pool, const void *p, const char *name)
{
    assert(pool != nullptr);
    assert(p != nullptr);
    assert(name != nullptr);

    if (find_attachment(pool, p) != nullptr)
        return;

    pool_attach(pool, p, name);
}

void
pool_detach(struct pool *pool, const void *p)
{
    struct attachment *attachment = find_attachment(pool, p);
    assert(attachment != nullptr);

    list_remove(&attachment->siblings);
    free(attachment);
}

void
pool_detach_checked(struct pool *pool, const void *p)
{
    struct attachment *attachment = find_attachment(pool, p);
    if (attachment == nullptr)
        return;

    list_remove(&attachment->siblings);
    free(attachment);
}

const char *
pool_attachment_name(struct pool *pool, const void *p)
{
    struct attachment *attachment = find_attachment(pool, p);
    return attachment != nullptr
        ? attachment->name
        : nullptr;
}

#endif
