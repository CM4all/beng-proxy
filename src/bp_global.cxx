/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_global.hxx"

struct tcache *global_translate_cache;

StockMap *global_tcp_stock;
struct tcp_balancer *global_tcp_balancer;

struct memcached_stock *global_memcached_stock;

struct http_cache *global_http_cache;

struct lhttp_stock *global_lhttp_stock;
struct fcgi_stock *global_fcgi_stock;

StockMap *global_was_stock;

struct filter_cache *global_filter_cache;

StockMap *global_delegate_stock;

struct nfs_stock *global_nfs_stock;
NfsCache *global_nfs_cache;

Stock *global_pipe_stock;
