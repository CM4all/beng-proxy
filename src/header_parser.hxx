/*
 * Parse HTTP headers into a strmap_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADER_PARSER_HXX
#define BENG_PROXY_HEADER_PARSER_HXX

#include <stddef.h>

struct pool;
class StringMap;
struct StringView;
class GrowingBuffer;

void
header_parse_line(struct pool &pool, StringMap &headers, StringView line);

void
header_parse_buffer(struct pool &pool, StringMap &headers,
                    GrowingBuffer &&gb);

#endif
