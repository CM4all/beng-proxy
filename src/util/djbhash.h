/*
 * Implementation of D. J. Bernstein's cdb hash function.
 * http://cr.yp.to/cdb/cdb.txt
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef DJB_HASH_H
#define DJB_HASH_H

#include <inline/compiler.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

gcc_pure
uint32_t
djb_hash(const void *p, size_t size);

gcc_pure
uint32_t
djb_hash_string(const char *p);

#ifdef __cplusplus
}
#endif

#endif
