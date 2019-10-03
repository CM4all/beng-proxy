/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pool.hxx"
#include "Ptr.hxx"
#include "Notify.hxx"
#include "LeakDetector.hxx"
#include "SlicePool.hxx"
#include "AllocatorStats.hxx"
#include "io/Logger.hxx"
#include "util/Recycler.hxx"
#include "util/Poison.hxx"

#include <boost/intrusive/list.hpp>

#ifdef VALGRIND
#include <valgrind/memcheck.h>
#endif

#include <forward_list>
#include <typeinfo>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef major
/* avoid name clash with system header macro */
#undef major
#endif

#if defined(DEBUG_POOL_GROW) || defined(DUMP_POOL_ALLOC_ALL)
#define DUMP_POOL_ALLOC
#endif

#if defined(__x86_64__) || defined(__PPC64__)
static constexpr unsigned ALIGN_BITS = 3;
#else
static constexpr unsigned ALIGN_BITS = 2;
#endif

static constexpr size_t ALIGN_SIZE = 1 << ALIGN_BITS;
static constexpr size_t ALIGN_MASK = ALIGN_SIZE - 1;

static constexpr unsigned RECYCLER_MAX_POOLS = 256;
static constexpr unsigned RECYCLER_MAX_LINEAR_AREAS = 256;

#ifndef NDEBUG
struct allocation_info {
    typedef boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> SiblingsHook;
    SiblingsHook siblings;

    size_t size;

    const char *type;

#ifdef TRACE
    const char *file;
    unsigned line;
#endif
};

struct attachment final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    const void *value;

    const char *name;
};

static constexpr size_t LINEAR_PREFIX = sizeof(struct allocation_info);
#else
static constexpr size_t LINEAR_PREFIX = 0;
#endif

enum pool_type {
    POOL_LIBC,
    POOL_LINEAR,
};

struct libc_pool_chunk {
    typedef boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> SiblingsHook;
    SiblingsHook siblings;

#ifdef POISON
    size_t size;
#endif
#ifndef NDEBUG
    struct allocation_info info;
#endif
    unsigned char data[sizeof(size_t)];

    struct Disposer {
        void operator()(struct libc_pool_chunk *chunk) noexcept {
#ifdef POISON
            static constexpr size_t LIBC_POOL_CHUNK_HEADER =
                offsetof(struct libc_pool_chunk, data);
            PoisonUndefined(chunk, LIBC_POOL_CHUNK_HEADER + chunk->size);
#endif
            free(chunk);
        }
    };
};

struct linear_pool_area {
    struct linear_pool_area *prev;

    /**
     * The slice_area that was used to allocated this pool area.  It
     * is nullptr if this area was allocated from the libc heap.
     */
    SliceArea *slice_area;

    size_t size, used;
    unsigned char data[sizeof(size_t)];
};

static const size_t LINEAR_POOL_AREA_HEADER =
    offsetof(struct linear_pool_area, data);

#ifdef DEBUG_POOL_REF
struct PoolRef {
#ifdef TRACE
    const char *file;
    unsigned line;
#endif

    unsigned count = 1;
};
#endif

struct pool final
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      LoggerDomainFactory {

    const LazyDomainLogger logger;

    typedef boost::intrusive::list<struct pool,
                                   boost::intrusive::constant_time_size<false>> List;

    List children;
#ifdef DEBUG_POOL_REF
    std::forward_list<PoolRef> refs, unrefs;
#endif
    struct pool *parent = nullptr;
    unsigned ref = 1;

    boost::intrusive::list<PoolNotify,
                           boost::intrusive::constant_time_size<false>> notify;

#ifndef NDEBUG
    bool trashed = false;

    /** this is a major pool, i.e. pool commits are performed after
        the major pool is freed */
    bool major;
#endif

    enum pool_type type;
    const char *const name;

    union CurrentArea {
        boost::intrusive::list<struct libc_pool_chunk,
                               boost::intrusive::member_hook<struct libc_pool_chunk,
                                                             libc_pool_chunk::SiblingsHook,
                                                             &libc_pool_chunk::siblings>,
                               boost::intrusive::constant_time_size<false>> libc;

        struct linear_pool_area *linear;

        CurrentArea() noexcept:libc() {}
        ~CurrentArea() {}
    } current_area;

#ifndef NDEBUG
    boost::intrusive::list<struct allocation_info,
                           boost::intrusive::member_hook<struct allocation_info,
                                                         allocation_info::SiblingsHook,
                                                         &allocation_info::siblings>,
                           boost::intrusive::constant_time_size<false>> allocations;

    boost::intrusive::list<PoolLeakDetector,
                           boost::intrusive::member_hook<PoolLeakDetector,
                                                         PoolLeakDetector::PoolLeakDetectorSiblingsHook,
                                                         &PoolLeakDetector::pool_leak_detector_siblings>,
                           boost::intrusive::constant_time_size<false>> leaks;

    boost::intrusive::list<struct attachment,
                           boost::intrusive::constant_time_size<false>> attachments;
#endif

    SlicePool *slice_pool;

    /**
     * The area size passed to pool_new_linear().
     */
    size_t area_size;

    /**
     * The number of bytes allocated from this pool, not counting
     * overhead and not counting p_free().
     */
    size_t netto_size = 0;

    explicit pool(const char *_name) noexcept
        :logger(*this), name(_name) {
    }

    pool(struct pool &&) = delete;
    pool &operator=(struct pool &&) = delete;

    /* virtual methods from class LoggerDomainFactory */
    std::string MakeLoggerDomain() const noexcept override {
        return std::string("pool ") + name;
    }
};

#ifndef NDEBUG
static pool::List trash;
#endif

static struct {
    Recycler<struct pool, RECYCLER_MAX_POOLS> pools;

    unsigned num_linear_areas;
    struct linear_pool_area *linear_areas;
} recycler;

static void * gcc_malloc
xmalloc(size_t size) noexcept
{
    void *p = malloc(size);
    if (gcc_unlikely(p == nullptr)) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

static constexpr size_t
align_size(size_t size) noexcept
{
    return ((size - 1) | ALIGN_MASK) + 1;
}

#ifndef NDEBUG
static struct allocation_info *
get_linear_allocation_info(void *p) noexcept
{
    void *q = (char *)p - sizeof(struct allocation_info);
    return (struct allocation_info *)q;
}
#endif

void
pool_recycler_clear(void) noexcept
{
    recycler.pools.Clear();

    while (recycler.linear_areas != nullptr) {
        struct linear_pool_area *linear = recycler.linear_areas;
        recycler.linear_areas = linear->prev;
        free(linear);
    }

    recycler.num_linear_areas = 0;
}

/**
 * @return true if the area was moved to the recycler, false if the
 * caller is responsible for freeing it
 */
static bool
pool_recycler_put_linear(struct linear_pool_area *area) noexcept
{
    assert(area != nullptr);
    assert(area->size > 0);
    assert(area->slice_area == nullptr);

    if (recycler.num_linear_areas >= RECYCLER_MAX_LINEAR_AREAS)
        return false;

    PoisonInaccessible(area->data, area->used);

    area->prev = recycler.linear_areas;
    recycler.linear_areas = area;
    ++recycler.num_linear_areas;
    return true;
}

static struct linear_pool_area *
pool_recycler_get_linear(size_t size) noexcept
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
pool_free_linear_area(struct linear_pool_area *area) noexcept
{
    assert(area->slice_area == nullptr);

    PoisonUndefined(area->data, area->used);
    free(area);
}

static bool
pool_dispose_slice_area(SlicePool *slice_pool,
                        struct linear_pool_area *area) noexcept
{
    if (area->slice_area == nullptr)
        return false;

    assert(slice_pool != nullptr);

    slice_pool->Free(*area->slice_area, area);
    return true;
}

static void
pool_dispose_linear_area(struct pool *pool,
                         struct linear_pool_area *area) noexcept
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
pool_add_child(struct pool *pool, struct pool *child) noexcept
{
    assert(child->parent == nullptr);

    child->parent = pool;

    pool->children.push_back(*child);
}

static inline void
pool_remove_child(struct pool *pool, struct pool *child) noexcept
{
    assert(child->parent == pool);

    pool->children.erase(pool->children.iterator_to(*child));
    child->parent = nullptr;
}

static struct pool *gcc_malloc
pool_new(struct pool *parent, const char *name) noexcept
{
    auto *pool = recycler.pools.Get(name);

#ifndef NDEBUG
    pool->major = parent == nullptr;
#endif

    if (parent != nullptr)
        pool_add_child(parent, pool);

#ifndef NDEBUG
    pool->major = parent == nullptr;
#endif

    return pool;
}

PoolPtr
pool_new_libc(struct pool *parent, const char *name) noexcept
{
    struct pool *pool = pool_new(parent, name);
    pool->type = POOL_LIBC;
    return PoolPtr(PoolPtr::donate, *pool);
}

gcc_malloc
static struct linear_pool_area *
pool_new_slice_area(SlicePool *slice_pool,
                    struct linear_pool_area *prev) noexcept
{
    auto allocation = slice_pool->Alloc();

    auto *area = (struct linear_pool_area *)allocation.Steal();
    assert(area != nullptr);

    area->prev = prev;
    area->slice_area = allocation.area;
    area->size = allocation.size - LINEAR_POOL_AREA_HEADER;
    area->used = 0;

    PoisonInaccessible(area->data, area->size);

    return area;
}

static struct linear_pool_area * gcc_malloc
pool_new_linear_area(struct linear_pool_area *prev, size_t size) noexcept
{
    struct linear_pool_area *area = (struct linear_pool_area *)
        xmalloc(LINEAR_POOL_AREA_HEADER + size);
    if (area == nullptr)
        abort();

    area->slice_area = nullptr;
    area->prev = prev;
    area->size = size;
    area->used = 0;

    PoisonInaccessible(area->data, area->size);

    return area;
}

static inline struct linear_pool_area *
pool_get_linear_area(struct linear_pool_area *prev, size_t size) noexcept
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

PoolPtr
pool_new_linear(struct pool *parent, const char *name,
                size_t initial_size) noexcept
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

    return PoolPtr(PoolPtr::donate, *pool);
#endif
}

PoolPtr
pool_new_slice(struct pool *parent, const char *name,
               SlicePool *slice_pool) noexcept
{
    assert(parent != nullptr);
    assert(slice_pool->GetSliceSize() > LINEAR_POOL_AREA_HEADER);

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
    pool->area_size = slice_pool->GetSliceSize() - LINEAR_POOL_AREA_HEADER;
    pool->slice_pool = slice_pool;
    pool->current_area.linear = nullptr;

    return PoolPtr(PoolPtr::donate, *pool);
#endif
}

#ifndef NDEBUG

#ifndef POOL_LIBC_ONLY

static bool
pool_linear_is_empty(const struct pool *pool) noexcept
{
    assert(pool->type == POOL_LINEAR);

    const struct linear_pool_area *area = pool->current_area.linear;
    return area == nullptr || (area->prev == nullptr && area->used == 0);
}

#endif

void
pool_set_major(struct pool *pool) noexcept
{
    assert(!pool->trashed);
    assert(pool->children.empty());

    pool->major = true;
}

#endif

#ifdef DUMP_POOL_ALLOC_ALL
static void
pool_dump_allocations(const struct pool &pool) noexcept;
#endif

static void
pool_check_leaks(const struct pool &pool) noexcept
{
#ifdef NDEBUG
    (void)pool;
#else
    if (pool.leaks.empty())
        return;

    pool.logger(1, "pool has leaked objects:");

    for (const auto &ld : pool.leaks)
        pool.logger.Format(1, " %p %s",
                           &ld, typeid(ld).name());

    abort();
#endif
}

static void
pool_check_attachments(const struct pool &pool) noexcept
{
#ifdef NDEBUG
    (void)pool;
#else
    if (pool.attachments.empty())
        return;

    pool.logger(1, "pool has attachments left:");

    for (const auto &attachment : pool.attachments)
        pool.logger.Format(1, " name='%s' value=%p",
                           attachment.name, attachment.value);

    abort();
#endif
}

static void
pool_destroy(struct pool *pool, gcc_unused struct pool *parent,
             struct pool *reparent_to) noexcept
{
    assert(pool->ref == 0);
    assert(pool->parent == nullptr);

#ifdef DUMP_POOL_SIZE
    pool->logger.Format(4, "pool size=%zu", pool->netto_size);
#endif

#ifdef DUMP_POOL_ALLOC_ALL
    pool_dump_allocations(*pool);
#endif

    pool_check_leaks(*pool);
    pool_check_attachments(*pool);

    pool->notify.clear();

#ifndef NDEBUG
    if (pool->trashed)
        trash.erase(trash.iterator_to(*pool));
#else
    TRACE_ARGS_IGNORE;
#endif

    while (!pool->children.empty()) {
        struct pool *child = &pool->children.front();
        pool_remove_child(pool, child);
        assert(child->ref > 0);

        if (reparent_to == nullptr) {
            /* children of major pools are put on trash, so they are
               collected by pool_commit() */
            assert(pool->major || pool->trashed);

#ifndef NDEBUG
            trash.push_front(*child);
            child->trashed = true;
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
    pool->refs.clear();
    pool->unrefs.clear();
#endif

    pool_clear(*pool);

    recycler.pools.Put(pool);
}

#ifdef DEBUG_POOL_REF
static void
pool_increment_ref(gcc_unused struct pool *pool,
                   std::forward_list<PoolRef> &list TRACE_ARGS_DECL) noexcept
{
#ifdef TRACE
    for (auto &ref : list) {
        if (ref.line == line && strcmp(ref.file, file) == 0) {
            ++ref.count;
            return;
        }
    }
#endif

    list.emplace_front();

#ifdef TRACE
    auto &ref = list.front();
    ref.file = file;
    ref.line = line;
#endif
}
#endif

#ifdef DEBUG_POOL_REF
static void
pool_dump_refs(const struct pool &pool) noexcept
{
    pool.logger.Format(0, "pool[%p](%u) REF:",
                       (const void *)&pool, pool.ref);

#ifdef TRACE
    for (auto &ref : pool.refs)
        pool.logger.Format(0, " %s:%u %u", ref.file, ref.line, ref.count);

    pool.logger(0, "UNREF:");
    for (auto &ref : pool.unrefs)
        pool.logger.Format(0, " %s:%u %u", ref.file, ref.line, ref.count);
#endif
}
#endif

void
pool_ref_impl(struct pool *pool TRACE_ARGS_DECL) noexcept
{
    assert(pool->ref > 0);
    ++pool->ref;

#ifdef POOL_TRACE_REF
    pool->logger(0, "pool_ref=", pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, pool->refs TRACE_ARGS_FWD);
#endif
}

unsigned
pool_unref_impl(struct pool *pool TRACE_ARGS_DECL) noexcept
{
    assert(pool->ref > 0);
    --pool->ref;

#ifdef POOL_TRACE_REF
    pool->logger(0, "pool_unref=", pool->ref);
#endif

#ifdef DEBUG_POOL_REF
    pool_increment_ref(pool, pool->unrefs TRACE_ARGS_FWD);
#endif

    if (gcc_unlikely(pool->ref == 0)) {
        struct pool *parent = pool->parent;
#ifdef NDEBUG
        struct pool *reparent_to = nullptr;
#else
        struct pool *reparent_to = pool->major ? nullptr : parent;
#endif
        if (parent != nullptr)
            pool_remove_child(parent, pool);
#ifdef DUMP_POOL_UNREF
        pool_dump_refs(*pool);
#endif
        pool_destroy(pool, parent, reparent_to);
        return 0;
    }

    return pool->ref;
}

size_t
pool_netto_size(const struct pool *pool) noexcept
{
    return pool->netto_size;
}

static size_t
pool_linear_brutto_size(const struct pool *pool) noexcept
{
    size_t size = 0;

    for (const struct linear_pool_area *area = pool->current_area.linear;
         area != nullptr; area = area->prev)
        size += area->size;

    return size;
}

size_t
pool_brutto_size(const struct pool *pool) noexcept
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
pool_recursive_netto_size(const struct pool *pool) noexcept
{
    return pool_netto_size(pool) + pool_children_netto_size(pool);
}

size_t
pool_recursive_brutto_size(const struct pool *pool) noexcept
{
    return pool_brutto_size(pool) + pool_children_brutto_size(pool);
}

size_t
pool_children_netto_size(const struct pool *pool) noexcept
{
    size_t size = 0;

    for (const auto &child : pool->children)
        size += pool_recursive_netto_size(&child);

    return size;
}

size_t
pool_children_brutto_size(const struct pool *pool) noexcept
{
    size_t size = 0;

    for (const auto &child : pool->children)
        size += pool_recursive_brutto_size(&child);

    return size;
}

AllocatorStats
pool_children_stats(const struct pool &pool) noexcept
{
    AllocatorStats stats;
    stats.netto_size = pool_children_netto_size(&pool);
    stats.brutto_size = pool_children_brutto_size(&pool);
    return stats;
}

static const char *
pool_type_string(enum pool_type type) noexcept
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
pool_dump_node(int indent, const struct pool &pool) noexcept
{
    pool.logger.Format(2, "%*spool '%s' type=%s ref=%u size=%zu p=%p",
                       indent, "",
                       pool.name, pool_type_string(pool.type),
                       pool.ref, pool.netto_size,
                       (const void *)&pool);

    indent += 2;
    for (const auto &child : pool.children)
        pool_dump_node(indent, child);
}

void
pool_dump_tree(const struct pool &pool) noexcept
{
    pool_dump_node(0, pool);
}

PoolNotify::PoolNotify(struct pool &pool) noexcept
{
    pool.notify.push_back(*this);
}

#ifndef NDEBUG

void
pool_trash(struct pool *pool) noexcept
{
    if (pool->trashed)
        return;

    assert(pool->parent != nullptr);

    pool_remove_child(pool->parent, pool);
    trash.push_front(*pool);
    pool->trashed = true;
}

void
pool_commit() noexcept
{
    if (trash.empty())
        return;

    LogConcat(0, "pool", "pool_commit(): there are unreleased pools in the trash:");

    for (const auto &pool : trash) {
#ifdef DEBUG_POOL_REF
        pool_dump_refs(pool);
#else
        LogFormat(0, "pool", "- '%s'(%u)", pool.name, pool.ref);
#endif
    }

    abort();
}

static bool
linear_pool_area_contains(const struct linear_pool_area *area,
                          const void *ptr, size_t size) noexcept
{
    return size <= area->used &&
        ptr >= (const void*)area->data &&
        ptr <= (const void*)(area->data + area->used - size);
}

bool
pool_contains(const struct pool &pool, const void *ptr, size_t size) noexcept
{
    assert(ptr != nullptr);
    assert(size > 0);

    if (pool.type != POOL_LINEAR)
        return true;

    for (const struct linear_pool_area *area = pool.current_area.linear;
         area != nullptr; area = area->prev)
        if (linear_pool_area_contains(area, ptr, size))
            return true;

    return false;
}

#endif

void
pool_clear(struct pool &pool) noexcept
{
    assert(pool.leaks.empty());
    assert(pool.attachments.empty());

    pool.allocations.clear();

    switch (pool.type) {
    case POOL_LIBC:
        pool.current_area.libc.clear_and_dispose(libc_pool_chunk::Disposer());
        break;

    case POOL_LINEAR:
        while (pool.current_area.linear != nullptr) {
            struct linear_pool_area *area = pool.current_area.linear;
            pool.current_area.linear = area->prev;
            pool_dispose_linear_area(&pool, area);
        }
        break;
    }
}

void
pool_mark(struct pool *pool, struct pool_mark_state *mark) noexcept
{
#ifndef POOL_LIBC_ONLY

#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        /* ignore on Valgrind */
        return;
#endif

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
pool_remove_allocations(struct pool *pool,
                        const unsigned char *p, size_t length) noexcept
{
#ifndef NDEBUG
    pool->allocations.remove_if([p, length](const struct allocation_info &info){
            return (const uint8_t *)&info >= p &&
                (const uint8_t *)(&info + 1) + info.size <= p + length;
        });
#else
    (void)pool;
    (void)p;
    (void)length;
#endif
}
#endif

void
pool_rewind(struct pool *pool, const struct pool_mark_state *mark) noexcept
{
#ifndef POOL_LIBC_ONLY

#ifdef VALGRIND
    if (RUNNING_ON_VALGRIND)
        /* ignore on Valgrind */
        return;
#endif

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

        PoisonInaccessible(marked_area->data + mark->position,
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
    assert(!pool_linear_is_empty(pool) || pool->allocations.empty());
#else
    (void)pool;
    (void)mark;
#endif
}

static void *
p_malloc_libc(struct pool *pool, size_t size TYPE_ARG_DECL TRACE_ARGS_DECL) noexcept
{
    const size_t aligned_size = align_size(size);
    struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)
        xmalloc(sizeof(*chunk) - sizeof(chunk->data) + aligned_size);

#ifndef NDEBUG
    pool->allocations.push_back(chunk->info);
    chunk->info.type = type;
#ifdef TRACE
    chunk->info.file = file;
    chunk->info.line = line;
#endif
    chunk->info.size = size;
#else
    TRACE_ARGS_IGNORE;
#endif

    pool->current_area.libc.push_back(*chunk);
#ifdef POISON
    chunk->size = size;
#endif
    return chunk->data;
}

#ifdef DUMP_POOL_ALLOC
static void
pool_dump_allocations(const struct pool &pool) noexcept
{
    size_t sum = 0;
    for (const auto &info : pool.allocations) {
        sum += info.size;
        pool.logger.Format(6, "- %s:%u %zu => %zu\n",
                           info.file, info.line, info.size, sum);
    }
}
#endif

static void *
p_malloc_linear(struct pool *pool, const size_t original_size
                TYPE_ARG_DECL TRACE_ARGS_DECL) noexcept
{
    auto &logger = pool->logger;
    struct linear_pool_area *area = pool->current_area.linear;

    size_t size = align_size(original_size);
    size += LINEAR_PREFIX;

    if (gcc_unlikely(size > pool->area_size)) {
        /* this allocation is larger than the standard area size;
           obtain a new area just for this allocation, and keep on
           using the last area */
        logger.Format(5, "big allocation on linear pool '%s' (%zu bytes)",
                      pool->name, original_size);
#ifdef DEBUG_POOL_GROW
        pool_dump_allocations(*pool);
        logger.Format(6, "+ %s:%u %zu", file, line, original_size);
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
    } else if (gcc_unlikely(area == nullptr || area->used + size > area->size)) {
        if (area != nullptr) {
            logger.Format(5, "growing linear pool '%s'", pool->name);
#ifdef DEBUG_POOL_GROW
            pool_dump_allocations(*pool);
            logger.Format(6, "+ %s:%u %zu", file, line, original_size);
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

    PoisonUndefined(p, size);

#ifndef NDEBUG
    struct allocation_info *info = (struct allocation_info *)p;
    info->type = type;
#ifdef TRACE
    info->file = file;
    info->line = line;
#endif
    info->size = original_size;
    pool->allocations.push_back(*info);
#endif

    return (char*)p + LINEAR_PREFIX;
}

static void *
internal_malloc(struct pool *pool, size_t size TYPE_ARG_DECL TRACE_ARGS_DECL) noexcept
{
    assert(pool != nullptr);

    pool->netto_size += size;

    if (gcc_likely(pool->type == POOL_LINEAR))
        return p_malloc_linear(pool, size TYPE_ARG_FWD TRACE_ARGS_FWD);

    assert(pool->type == POOL_LIBC);
    return p_malloc_libc(pool, size TYPE_ARG_FWD TRACE_ARGS_FWD);
}

void *
p_malloc_impl(struct pool *pool, size_t size TYPE_ARG_DECL TRACE_ARGS_DECL) noexcept
{
    return internal_malloc(pool, size TYPE_ARG_FWD TRACE_ARGS_FWD);
}

static void
p_free_libc(struct pool *pool, void *ptr)
{
    void *q = (char *)ptr - offsetof(struct libc_pool_chunk, data);
    struct libc_pool_chunk *chunk = (struct libc_pool_chunk *)q;

#ifndef NDEBUG
    pool->allocations.erase(pool->allocations.iterator_to(chunk->info));
#endif

    pool->current_area.libc.erase_and_dispose(pool->current_area.libc.iterator_to(*chunk),
                                              libc_pool_chunk::Disposer());
}

void
p_free(struct pool *pool, const void *cptr) noexcept
{
    /* deconst hack - we know what we're doing![tm] */
    union {
        const void *in;
        void *out;
    } u = { .in = cptr };
    void *ptr = u.out;

    assert(pool != nullptr);
    assert(ptr != nullptr);
    assert((((unsigned long)ptr) & ALIGN_MASK) == 0);
    assert(pool_contains(*pool, ptr, 1));

    if (pool->type == POOL_LIBC)
        p_free_libc(pool, ptr);
#ifndef NDEBUG
    else if (pool->type == POOL_LINEAR) {
        struct allocation_info *info = get_linear_allocation_info(ptr);
        pool->allocations.erase(pool->allocations.iterator_to(*info));
        PoisonInaccessible(ptr, info->size);
    }
#endif
    else
        /* we don't know the exact size of this buffer, so we only
           mark the first ALIGN bytes */
        PoisonInaccessible(ptr, ALIGN_SIZE);
}

#ifndef NDEBUG

void
pool_register_leak_detector(struct pool &pool, PoolLeakDetector &ld) noexcept
{
    pool.leaks.push_back(ld);
}

void
pool_attach(struct pool *pool, const void *p, const char *name) noexcept
{
    assert(pool != nullptr);
    assert(p != nullptr);
    assert(name != nullptr);

    struct attachment *attachment = (struct attachment *)
        xmalloc(sizeof(*attachment));
    attachment->value = p;
    attachment->name = name;

    pool->attachments.push_back(*attachment);
}

static struct attachment *
find_attachment(struct pool *pool, const void *p) noexcept
{
    for (auto &attachment : pool->attachments)
        if (attachment.value == p)
            return &attachment;

    return nullptr;
}

void
pool_attach_checked(struct pool *pool, const void *p,
                    const char *name) noexcept
{
    assert(pool != nullptr);
    assert(p != nullptr);
    assert(name != nullptr);

    if (find_attachment(pool, p) != nullptr)
        return;

    pool_attach(pool, p, name);
}

void
pool_detach(struct pool *pool, const void *p) noexcept
{
    struct attachment *attachment = find_attachment(pool, p);
    assert(attachment != nullptr);

    pool->attachments.erase_and_dispose(pool->attachments.iterator_to(*attachment),
                                        free);
}

#endif
