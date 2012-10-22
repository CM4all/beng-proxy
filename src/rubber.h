/*
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RUBBER_H
#define BENG_PROXY_RUBBER_H

#include <inline/compiler.h>

#include <stddef.h>

struct rubber;

struct rubber *
rubber_new(size_t size);

void
rubber_free(struct rubber *r);

/**
 * Add a new object with the specified size.  Use rubber_write() to
 * actually copy data to the object.
 *
 * @return the object id, or 0 on error
 */
unsigned
rubber_add(struct rubber *r, size_t size);

/**
 * Return a writable pointer to the object.
 */
gcc_pure
void *
rubber_write(struct rubber *r, unsigned id);

/**
 * Return a read-only pointer to the object.
 */
gcc_pure
const void *
rubber_read(const struct rubber *r, unsigned id);

/**
 * Shrink an object.  The new size must be smaller (or equal) to the
 * current size.  This is done in-place, possibly leaving a gap that
 * can only be used again after rubber_compress() has been called.
 */
void
rubber_shrink(struct rubber *r, unsigned id, size_t new_size);

void
rubber_remove(struct rubber *r, unsigned id);

/**
 * Returns the total size of all allocations.
 */
gcc_pure
size_t
rubber_get_netto_size(const struct rubber *r);

/**
 * Returns the memory consumed by this object, not including the
 * allocation table.
 */
gcc_pure
size_t
rubber_get_brutto_size(const struct rubber *r);

void
rubber_compress(struct rubber *rr);

#endif
