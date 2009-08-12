/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "global.h"

struct tcache *global_translate_cache;

struct hstock *global_tcp_stock;

struct memcached_stock *global_memcached_stock;

struct http_cache *global_http_cache;

struct fcgi_stock *global_fcgi_stock;

struct filter_cache *global_filter_cache;

struct hstock *global_delegate_stock;
