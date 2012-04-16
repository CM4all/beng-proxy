/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_REPLACE_H
#define BENG_PROXY_ISTREAM_REPLACE_H

#include <sys/types.h>

struct pool;
struct istream;

struct istream *
istream_replace_new(struct pool *pool, struct istream *input);

void
istream_replace_add(struct istream *istream, off_t start, off_t end,
                    struct istream *contents);

void
istream_replace_finish(struct istream *istream);

#endif
