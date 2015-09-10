/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_ITEM_HXX
#define BENG_PROXY_STOCK_ITEM_HXX

#include <boost/intrusive/list.hpp>

struct pool;
struct Stock;
class StockGetHandler;

struct StockItem
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    Stock *stock;
    struct pool *pool;

    StockGetHandler *handler;

    /**
     * If true, then this object will never be reused.
     */
    bool fade;

#ifndef NDEBUG
    bool is_idle;
#endif
};

#endif
