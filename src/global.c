/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "global.h"

struct tcache *global_translate_cache;

struct hstock *global_tcp_stock;
struct tcp_balancer *global_tcp_balancer;

struct memcached_stock *global_memcached_stock;

struct http_cache *global_http_cache;

struct hstock *global_fcgi_stock;

struct hstock *global_was_stock;

struct filter_cache *global_filter_cache;

struct hstock *global_delegate_stock;

struct nfs_stock *global_nfs_stock;

struct stock *global_pipe_stock;
