/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_REPLACE_HXX
#define BENG_PROXY_ISTREAM_REPLACE_HXX

#include <sys/types.h>

struct pool;
class Istream;

Istream *
istream_replace_new(struct pool &pool, Istream &input);

void
istream_replace_add(Istream &istream, off_t start, off_t end,
                    Istream *contents);

/**
 * Extend the end position of the latest replacement.
 *
 * @param start the start value that was passed to
 * istream_replace_add()
 * @param end the new end position; it must not be smaller than the
 * current end position of the replacement
 */
void
istream_replace_extend(Istream &istream, off_t start, off_t end);

/**
 * Mark all source data until the given offset as "settled",
 * i.e. there will be no more substitutions before this offset.  It
 * allows this object to deliver data until this offset to its
 * handler.
 */
void
istream_replace_settle(Istream &istream, off_t offset);

void
istream_replace_finish(Istream &istream);

#endif
