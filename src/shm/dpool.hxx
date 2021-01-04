/*
 * Copyright 2007-2021 CM4all GmbH
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

/*
 * Distributed memory pool in shared memory.
 */

#ifndef SHM_DPOOL_HXX
#define SHM_DPOOL_HXX

#include <stddef.h>

#include <utility>
#include <new>

struct dpool;
struct shm;
struct StringView;

/**
 * Create a new memory pool.
 *
 * @return the new dpool object, or NULL if the shm object has no free
 * space
 */
struct dpool *
dpool_new(struct shm &shm);

/**
 * Destroy the memory pool.  All allocated memory is freed.
 */
void
dpool_destroy(struct dpool *pool);

bool
dpool_is_fragmented(const struct dpool &pool);

/**
 * Allocate memory from the pool.
 *
 * Throws std::bad_alloc on error.
 *
 * @return a pointer to the start, or NULL if allocation failed.
 */
void *
d_malloc(struct dpool &pool, size_t size);

/**
 * Frees the memory previously allocated by d_malloc().
 */
void
d_free(struct dpool &pool, const void *p);

/**
 * Duplicate a chunk of memory, allocating the new pointer from the
 * pool.
 */
char *
d_memdup(struct dpool &pool, const void *src, size_t length);

/**
 * Duplicate a C string, allocating the new pointer from the pool.
 */
char *
d_strdup(struct dpool &pool, const char *src);

char *
d_strdup(struct dpool &pool, StringView src);

static inline char *
d_strdup_checked(struct dpool &pool, const char *src)
{
	return src != NULL ? d_strdup(pool, src) : NULL;
}

/**
 * Duplicate a string, allocating the new pointer from the pool.
 */
char *
d_strndup(struct dpool &pool, const char *src, size_t length);

/**
 * Duplicate data in the given #StringView.  If src.IsNull(), then
 * nullptr is returned; if src.IsEmpty(), then an empty string literal
 * is returned.
 *
 * Throws std::bad_alloc on error.
 */
StringView
DupStringView(struct dpool &pool, StringView src);

/**
 * Free the data allocated by the given #StringView.  It is a no-op if
 * src.IsEmpty() because then it's either nullptr or the empty string
 * literal.
 */
void
FreeStringView(struct dpool &pool, StringView s);

template<typename T, typename... Args>
T *
NewFromPool(struct dpool &pool, Args&&... args)
{
	void *t = d_malloc(pool, sizeof(T));

	try {
		return ::new(t) T(std::forward<Args>(args)...);
	} catch (...) {
		d_free(pool, t);
		throw;
	}
}

template<typename T>
void
DeleteFromPool(struct dpool &pool, T *t)
{
	t->~T();
	d_free(pool, t);
}

template<typename T>
void
DeleteDestroyPool(struct dpool &pool, T *t)
{
	t->~T();
	dpool_destroy(&pool);
}

#endif
