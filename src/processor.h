/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROCESSOR_H
#define __BENG_PROCESSOR_H

#include "pool.h"
#include "istream.h"

#include <sys/types.h>

istream_t attr_malloc
processor_new(pool_t pool, istream_t istream);

#endif
