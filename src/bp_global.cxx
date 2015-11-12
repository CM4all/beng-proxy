/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_global.hxx"

struct tcache *global_translate_cache;

StockMap *global_tcp_stock;
TcpBalancer *global_tcp_balancer;

struct memcached_stock *global_memcached_stock;

HttpCache *global_http_cache;

LhttpStock *global_lhttp_stock;
FcgiStock *global_fcgi_stock;

StockMap *global_was_stock;

FilterCache *global_filter_cache;

StockMap *global_delegate_stock;

NfsStock *global_nfs_stock;
NfsCache *global_nfs_cache;

Stock *global_pipe_stock;
