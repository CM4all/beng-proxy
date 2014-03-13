/*
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RUBBER_HXX
#define BENG_PROXY_RUBBER_HXX

#include <inline/compiler.h>

#include <stdbool.h>
#include <stddef.h>

class Rubber;

Rubber *
rubber_new(size_t size);

void
rubber_free(Rubber *r);

/**
 * Controls whether forked child processes inherit the allocator.
 * This is enabled by default.
 */
void
rubber_fork_cow(Rubber *r, bool inherit);

/**
 * Add a new object with the specified size.  Use rubber_write() to
 * actually copy data to the object.
 *
 * @param size the size, must be positive
 * @return the object id, or 0 on error
 */
unsigned
rubber_add(Rubber *r, size_t size);

/**
 * Returns the size of an allocation.  Due to padding, the returned
 * value may be a bit bigger than the size that was passed to
 * rubber_add().
 */
gcc_pure
size_t
rubber_size_of(const Rubber *r, unsigned id);

/**
 * Return a writable pointer to the object.
 */
gcc_pure
void *
rubber_write(Rubber *r, unsigned id);

/**
 * Return a read-only pointer to the object.
 */
gcc_pure
const void *
rubber_read(const Rubber *r, unsigned id);

/**
 * Shrink an object.  The new size must be smaller (or equal) to the
 * current size.  This is done in-place, possibly leaving a gap that
 * can only be used again after rubber_compress() has been called.
 *
 * @param new_size the new size, must be positive
 */
void
rubber_shrink(Rubber *r, unsigned id, size_t new_size);

void
rubber_remove(Rubber *r, unsigned id);

/**
 * Returns the maximum total size of all allocations.
 */
gcc_pure
size_t
rubber_get_max_size(const Rubber *r);

/**
 * Returns the total size of all allocations.
 */
gcc_pure
size_t
rubber_get_netto_size(const Rubber *r);

/**
 * Returns the memory consumed by this object, not including the
 * allocation table.
 */
gcc_pure
size_t
rubber_get_brutto_size(const Rubber *r);

void
rubber_compress(Rubber *rr);

#endif
