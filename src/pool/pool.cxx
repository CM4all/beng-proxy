// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "pool.hxx"
#include "Ptr.hxx"
#include "LeakDetector.hxx"
#include "memory/Checker.hxx"
#include "memory/SlicePool.hxx"
#include "memory/AllocatorStats.hxx"
#include "io/Logger.hxx"
#include "util/Compiler.h"
#include "util/IntrusiveList.hxx"
#include "util/Recycler.hxx"
#include "util/RoundPowerOfTwo.hxx"
#include "util/Poison.hxx"

#include <fmt/format.h>

#include <forward_list>
#include <typeinfo>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef major
/* avoid name clash with system header macro */
#undef major
#endif

// historic debugging features:
//#define DEBUG_POOL_REF
//#define DEBUG_POOL_GROW
//#define DUMP_POOL_ALLOC_ALL

#if defined(DEBUG_POOL_GROW) || defined(DUMP_POOL_ALLOC_ALL)
#define DUMP_POOL_ALLOC
#endif

using std::string_view_literals::operator""sv;

static constexpr size_t ALIGN_SIZE = RoundUpToPowerOfTwo(alignof(std::max_align_t));

static constexpr unsigned RECYCLER_MAX_POOLS = 256;
static constexpr unsigned RECYCLER_MAX_LINEAR_AREAS = 256;

#ifndef NDEBUG
struct alignas(std::max_align_t) allocation_info {
	IntrusiveListHook<IntrusiveHookMode::NORMAL> siblings;

	size_t size;

	const char *type;

#ifdef ENABLE_TRACE
	const char *file;
	unsigned line;
#endif
};

static constexpr size_t LINEAR_PREFIX = sizeof(struct allocation_info);
#else
static constexpr size_t LINEAR_PREFIX = 0;
#endif

struct libc_pool_chunk {
	IntrusiveListHook<IntrusiveHookMode::NORMAL> siblings;

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
#ifdef ENABLE_TRACE
	const char *file;
	unsigned line;
#endif

	unsigned count = 1;
};
#endif

struct pool final
	: IntrusiveListHook<IntrusiveHookMode::NORMAL>,
	  LoggerDomainFactory {

	enum class Type : uint_least8_t {
		DUMMY,
		LIBC,
		LINEAR,
	};

	const LazyDomainLogger logger{*this};

	using List = IntrusiveList<struct pool>;

	List children;
#ifdef DEBUG_POOL_REF
	std::forward_list<PoolRef> refs, unrefs;
#endif
	struct pool *parent = nullptr;
	unsigned ref = 1;

#ifndef NDEBUG
	bool trashed = false;

	/** this is a major pool, i.e. pool commits are performed after
	    the major pool is freed */
	bool major;
#endif

	const Type type;

	const char *const name;

	union CurrentArea {
		IntrusiveList<struct libc_pool_chunk,
			      IntrusiveListMemberHookTraits<&libc_pool_chunk::siblings>> libc;

		struct linear_pool_area *linear;

		CurrentArea() noexcept:libc() {}
		~CurrentArea() {}
	} current_area;

#ifndef NDEBUG
	IntrusiveList<struct allocation_info,
		      IntrusiveListMemberHookTraits<&allocation_info::siblings>> allocations;

	IntrusiveList<PoolLeakDetector> leaks;
#endif

	SlicePool *slice_pool;

	/**
	 * The area size passed to pool_new_linear().
	 */
	size_t area_size;

	/**
	 * The number of bytes allocated from this pool, not counting
	 * overhead.
	 */
	size_t netto_size = 0;

	pool(Type _type, const char *_name) noexcept
		:type(_type), name(_name) {
	}

	pool(struct pool &&) = delete;
	pool &operator=(struct pool &&) = delete;

	void AddChild(struct pool &child) noexcept {
		assert(child.parent == nullptr);

		child.parent = this;
		children.push_back(child);
	}

	void RemoveChild(struct pool &child) noexcept {
		assert(child.parent == this);

		child.unlink();
		child.parent = nullptr;
	}

	/* virtual methods from class LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override {
		return fmt::format("pool {}", name);
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

[[gnu::malloc]]
static void *
xmalloc(size_t size) noexcept
{
	void *p = malloc(size);
	if (p == nullptr) [[unlikely]] {
		fputs("Out of memory\n", stderr);
		abort();
	}
	return p;
}

static constexpr size_t
align_size(size_t size) noexcept
{
	return RoundUpToPowerOfTwo(size, ALIGN_SIZE);
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

[[gnu::malloc]]
static struct pool *
pool_new(struct pool *parent, pool::Type type, const char *name) noexcept
{
	auto *pool = recycler.pools.Get(type, name);

#ifndef NDEBUG
	pool->major = parent == nullptr;
#endif

	if (parent != nullptr)
		parent->AddChild(*pool);

#ifndef NDEBUG
	pool->major = parent == nullptr;
#endif

	return pool;
}

PoolPtr
pool_new_dummy(struct pool *parent, const char *name) noexcept
{
	struct pool *pool = pool_new(parent, pool::Type::DUMMY, name);
	return PoolPtr(PoolPtr::donate, *pool);
}

PoolPtr
pool_new_libc(struct pool *parent, const char *name) noexcept
{
	struct pool *pool = pool_new(parent, pool::Type::LIBC, name);
	return PoolPtr(PoolPtr::donate, *pool);
}

[[gnu::malloc]]
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

[[gnu::malloc]]
static struct linear_pool_area *
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
	if (HaveMemoryChecker())
		/* Valgrind cannot verify allocations and memory accesses with
		   this library; therefore use the "libc" pool when running on
		   valgrind */
		return pool_new_libc(parent, name);

	struct pool *pool = pool_new(parent, pool::Type::LINEAR, name);
	pool->area_size = initial_size;
	pool->slice_pool = nullptr;
	pool->current_area.linear = nullptr;

	assert(parent != nullptr);

	return PoolPtr(PoolPtr::donate, *pool);
}

PoolPtr
pool_new_slice(struct pool &parent, const char *name,
	       SlicePool &slice_pool) noexcept
{
	assert(slice_pool.GetSliceSize() > LINEAR_POOL_AREA_HEADER);

	if (HaveMemoryChecker())
		/* Valgrind cannot verify allocations and memory accesses with
		   this library; therefore use the "libc" pool when running on
		   valgrind */
		return pool_new_libc(&parent, name);

	struct pool *pool = pool_new(&parent, pool::Type::LINEAR, name);
	pool->area_size = slice_pool.GetSliceSize() - LINEAR_POOL_AREA_HEADER;
	pool->slice_pool = &slice_pool;
	pool->current_area.linear = nullptr;

	return PoolPtr(PoolPtr::donate, *pool);
}

#ifndef NDEBUG

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
		pool.logger.Fmt(1, " {} {}"sv,
				fmt::ptr(&ld), typeid(ld).name());

	abort();
#endif
}

static void
pool_destroy(struct pool *pool, struct pool *reparent_to) noexcept
{
	assert(pool->ref == 0);
	assert(pool->parent == nullptr);

#ifdef DUMP_POOL_SIZE
	pool->logger.Fmt(4, "pool size={}"sv, pool->netto_size);
#endif

#ifdef DUMP_POOL_ALLOC_ALL
	pool_dump_allocations(*pool);
#endif

	pool_check_leaks(*pool);

#ifndef NDEBUG
	if (pool->trashed)
		pool->unlink();
#else
	TRACE_ARGS_IGNORE;
#endif

	while (!pool->children.empty()) {
		struct pool *child = &pool->children.front();
		pool->RemoveChild(*child);
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

			reparent_to->AddChild(*child);
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
pool_increment_ref(std::forward_list<PoolRef> &list TRACE_ARGS_DECL) noexcept
{
#ifdef ENABLE_TRACE
	for (auto &ref : list) {
		if (ref.line == line && strcmp(ref.file, file) == 0) {
			++ref.count;
			return;
		}
	}
#endif

	list.emplace_front();

#ifdef ENABLE_TRACE
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
	pool.logger.Fmt(0, "pool[{}]({}) REF:"sv,
			fmt::ptr(&pool), pool.ref);

#ifdef ENABLE_TRACE
	for (auto &ref : pool.refs)
		pool.logger.Fmt(0, " {}:{} {}"sv, ref.file, ref.line, ref.count);

	pool.logger(0, "UNREF:");
	for (auto &ref : pool.unrefs)
		pool.logger.Fmt(0, " {}:{} {}"sv, ref.file, ref.line, ref.count);
#endif
}
#endif

void
pool_ref(struct pool *pool TRACE_ARGS_DECL) noexcept
{
	assert(pool->ref > 0);
	++pool->ref;

#ifdef POOL_TRACE_REF
	pool->logger(0, "pool_ref=", pool->ref);
#endif

#ifdef DEBUG_POOL_REF
	pool_increment_ref(pool->refs TRACE_ARGS_FWD);
#endif
}

unsigned
pool_unref(struct pool *pool TRACE_ARGS_DECL) noexcept
{
	assert(pool->ref > 0);
	--pool->ref;

#ifdef POOL_TRACE_REF
	pool->logger(0, "pool_unref=", pool->ref);
#endif

#ifdef DEBUG_POOL_REF
	pool_increment_ref(pool->unrefs TRACE_ARGS_FWD);
#endif

	if (pool->ref == 0) [[unlikely]] {
		struct pool *parent = pool->parent;
#ifdef NDEBUG
		struct pool *reparent_to = nullptr;
#else
		struct pool *reparent_to = pool->major ? nullptr : parent;
#endif
		if (parent != nullptr)
			parent->RemoveChild(*pool);
#ifdef DUMP_POOL_UNREF
		pool_dump_refs(*pool);
#endif
		pool_destroy(pool, reparent_to);
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
	case pool::Type::DUMMY:
		return 0;

	case pool::Type::LIBC:
		return pool_netto_size(pool);

	case pool::Type::LINEAR:
		return pool_linear_brutto_size(pool);
	}

	assert(false);
	return 0;
}

AllocatorStats
pool_stats(const struct pool &pool) noexcept
{
	return {
		.brutto_size = pool_brutto_size(&pool),
		.netto_size = pool_netto_size(&pool),
	};
}

#ifndef NDEBUG

void
pool_trash(struct pool *pool) noexcept
{
	if (pool->trashed)
		return;

	assert(pool->parent != nullptr);

	pool->parent->RemoveChild(*pool);
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
		LogFmt(0, "pool", "- '{}'({})"sv, pool.name, pool.ref);
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

	if (pool.type != pool::Type::LINEAR)
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

#ifndef NDEBUG
	pool.allocations.clear();
#endif

	switch (pool.type) {
	case pool::Type::DUMMY:
		break;

	case pool::Type::LIBC:
		pool.current_area.libc.clear_and_dispose(libc_pool_chunk::Disposer());
		break;

	case pool::Type::LINEAR:
		while (pool.current_area.linear != nullptr) {
			struct linear_pool_area *area = pool.current_area.linear;
			pool.current_area.linear = area->prev;
			pool_dispose_linear_area(&pool, area);
		}
		break;
	}
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
#ifdef ENABLE_TRACE
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
		pool.logger.Fmt(6, "- {}:{} {} [{}] => {}"sv,
				info.file, info.line, info.size, info.type ? info.type : "", sum);
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

	if (size > pool->area_size) [[unlikely]] {
		/* this allocation is larger than the standard area size;
		   obtain a new area just for this allocation, and keep on
		   using the last area */
		logger.Fmt(5, "big allocation on linear pool '{}' ({} bytes)"sv,
			      pool->name, original_size);
#ifdef DEBUG_POOL_GROW
		pool_dump_allocations(*pool);
		logger.Fmt(6, "+ {}:{} {}"sv, file, line, original_size);
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
	} else if (area == nullptr || area->used + size > area->size) [[unlikely]] {
		if (area != nullptr) {
			logger.Fmt(5, "growing linear pool '{}'"sv, pool->name);
#ifdef DEBUG_POOL_GROW
			pool_dump_allocations(*pool);
			logger.Fmt(6, "+ {}:{} {}"sv, file, line, original_size);
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
#ifdef ENABLE_TRACE
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

	if (pool->type == pool::Type::LINEAR) [[likely]]
		return p_malloc_linear(pool, size TYPE_ARG_FWD TRACE_ARGS_FWD);

	assert(pool->type == pool::Type::LIBC);
	return p_malloc_libc(pool, size TYPE_ARG_FWD TRACE_ARGS_FWD);
}

void *
p_malloc(struct pool *pool, size_t size TYPE_ARG_DECL TRACE_ARGS_DECL) noexcept
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
p_free(struct pool *pool, const void *cptr, size_t size) noexcept
{
	void *ptr = const_cast<void *>(cptr);

	assert(pool != nullptr);
	assert(ptr != nullptr);
	assert(reinterpret_cast<intptr_t>(ptr) % ALIGN_SIZE == 0);
	assert(pool_contains(*pool, ptr, size));

	switch (pool->type) {
	case pool::Type::DUMMY:
		assert(false);
		gcc_unreachable();

	case pool::Type::LIBC:
		p_free_libc(pool, ptr);
		break;

	case pool::Type::LINEAR:
#ifndef NDEBUG
		{
			struct allocation_info *info = get_linear_allocation_info(ptr);
			assert(size == info->size);
			pool->allocations.erase(pool->allocations.iterator_to(*info));
		}
#endif
		PoisonInaccessible(ptr, size);
		break;
	}

	pool->netto_size -= size;
}

#ifndef NDEBUG

void
pool_register_leak_detector(struct pool &pool, PoolLeakDetector &ld) noexcept
{
	pool.leaks.push_back(ld);
}

#endif
