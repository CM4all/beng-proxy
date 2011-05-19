/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GLOBAL_H
#define __BENG_GLOBAL_H

extern struct tcache *global_translate_cache;

extern struct hstock *global_tcp_stock;
extern struct tcp_balancer *global_tcp_balancer;

extern struct memcached_stock *global_memcached_stock;

extern struct http_cache *global_http_cache;

extern struct hstock *global_fcgi_stock;

extern struct hstock *global_was_stock;

extern struct filter_cache *global_filter_cache;

extern struct hstock *global_delegate_stock;

extern struct stock *global_pipe_stock;

#endif
