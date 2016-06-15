/*
 * Launch processes and connect a stream socket to them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_STOCK_HXX
#define BENG_PROXY_CHILD_STOCK_HXX

#include "FdType.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <sys/socket.h>

class StockMap;
struct StockItem;
struct PreparedChildProcess;
class EventLoop;
class SpawnService;

struct ChildStockClass {
    int (*socket_type)(void *info);
    bool (*prepare)(void *info, int fd,
                    PreparedChildProcess &p,
                    GError **error_r);
};

StockMap *
child_stock_new(unsigned limit, unsigned max_idle,
                EventLoop &event_loop, SpawnService &spawn_service,
                const ChildStockClass *cls);

void
child_stock_free(StockMap *stock);

/**
 * Connect a socket to the given child process.  The socket must be
 * closed before the #stock_item is returned.
 *
 * @return a socket descriptor or -1 on error
 */
int
child_stock_item_connect(StockItem *item,
                         GError **error_r);

gcc_pure
static inline FdType
child_stock_item_get_type(gcc_unused const StockItem *item)
{
    return FdType::FD_SOCKET;
}

#endif
