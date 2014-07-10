/*
 * Copy headers from one strmap to another.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADER_COPY_H
#define BENG_PROXY_HEADER_COPY_H

struct strmap;

void
header_copy_one(const struct strmap *in, struct strmap *out, const char *key);

void
header_copy_list(const struct strmap *in, struct strmap *out,
                 const char *const*keys);

/**
 * Copy all headers beginning with a certain prefix.
 */
void
header_copy_prefix(const struct strmap *in, struct strmap *out,
                   const char *prefix);

#endif
