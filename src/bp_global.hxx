/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GLOBAL_H
#define __BENG_GLOBAL_H

struct Stock;
struct StockMap;
class FilterCache;
struct TcpBalancer;
struct LhttpStock;
struct FcgiStock;
struct NfsCache;

extern struct tcache *global_translate_cache;

extern StockMap *global_tcp_stock;
extern TcpBalancer *global_tcp_balancer;

extern struct memcached_stock *global_memcached_stock;

extern struct http_cache *global_http_cache;

extern LhttpStock *global_lhttp_stock;
extern FcgiStock *global_fcgi_stock;

extern StockMap *global_was_stock;

extern FilterCache *global_filter_cache;

extern StockMap *global_delegate_stock;

extern struct nfs_stock *global_nfs_stock;
extern NfsCache *global_nfs_cache;

extern Stock *global_pipe_stock;

#endif
