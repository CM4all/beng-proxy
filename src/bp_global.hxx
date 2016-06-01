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

extern Stock *global_pipe_stock;

#endif
