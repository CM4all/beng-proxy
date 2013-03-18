/*
 * An istream which duplicates data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_TEE_H
#define BENG_PROXY_ISTREAM_TEE_H

#include <stdbool.h>

struct pool;

/**
 * Create two new streams fed from one input.
 *
 * @param input the istream which is duplicated
 * @param first_weak if true, closes the whole object if only the
 * first output remains
 * @param second_weak if true, closes the whole object if only the
 * second output remains
 */
struct istream *
istream_tee_new(struct pool *pool, struct istream *input,
                bool first_weak, bool second_weak);

struct istream *
istream_tee_second(struct istream *istream);

#endif
