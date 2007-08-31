/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROCESSOR_H
#define __BENG_PROCESSOR_H

#include "istream.h"
#include "strmap.h"

#include <sys/types.h>

istream_t attr_malloc
processor_new(pool_t pool, istream_t istream, const char *base_uri,
              strmap_t args);

#endif
