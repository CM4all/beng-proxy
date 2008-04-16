/*
 * Shared memory for sharing data between worker processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SHM_H
#define __BENG_SHM_H

#include <stddef.h>

struct shm;

struct shm *
shm_new(size_t page_size, unsigned num_pages);

void
shm_close(struct shm *shm);

size_t
shm_page_size(const struct shm *shm);

void *
shm_alloc(struct shm *shm);

void
shm_free(struct shm *shm, const void *p);

#endif
