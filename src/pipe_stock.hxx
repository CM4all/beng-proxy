/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PIPE_STOCK_HXX
#define BENG_PROXY_PIPE_STOCK_HXX

class Stock;
struct StockItem;

Stock *
pipe_stock_new();

void
pipe_stock_free(Stock *stock);

void
pipe_stock_item_get(StockItem *item, int fds[2]);

#endif
