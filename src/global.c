/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "global.h"

struct tcache *global_translate_cache;

struct hstock *global_tcp_stock;

struct http_cache *global_http_cache;
