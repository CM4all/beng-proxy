/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADER_PARSER_HXX
#define BENG_PROXY_HEADER_PARSER_HXX

#include <stddef.h>

struct pool;
struct strmap;
struct StringView;
class GrowingBuffer;

void
header_parse_line(struct pool &pool, struct strmap *headers, StringView line);

void
header_parse_buffer(struct pool *pool, struct strmap *headers,
                    const GrowingBuffer *gb);

#endif
