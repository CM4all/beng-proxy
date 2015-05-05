/*
 * This istream filter substitutes a word with another string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_SUBST_HXX
#define BENG_PROXY_ISTREAM_SUBST_HXX

#include <stddef.h>

struct pool;
struct istream;

struct istream *
istream_subst_new(struct pool *pool, struct istream *input);

bool
istream_subst_add_n(struct istream *istream, const char *a,
                    const char *b, size_t b_length);

bool
istream_subst_add(struct istream *istream, const char *a, const char *b);

#endif
