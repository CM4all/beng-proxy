/*
 * This istream filter substitutes a word with another string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_SUBST_HXX
#define BENG_PROXY_ISTREAM_SUBST_HXX

#include <stddef.h>

struct pool;
class Istream;

Istream *
istream_subst_new(struct pool *pool, Istream &input);

bool
istream_subst_add_n(Istream &istream, const char *a,
                    const char *b, size_t b_length);

bool
istream_subst_add(Istream &istream, const char *a, const char *b);

#endif
