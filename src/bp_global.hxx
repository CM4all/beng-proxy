/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GLOBAL_H
#define __BENG_GLOBAL_H

class Stock;
struct StockMap;
class HttpCache;
class FilterCache;

extern struct tcache *global_translate_cache;

extern HttpCache *global_http_cache;

extern FilterCache *global_filter_cache;

extern StockMap *global_delegate_stock;

extern Stock *global_pipe_stock;

#endif
