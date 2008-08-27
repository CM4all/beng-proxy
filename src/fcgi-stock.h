/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FCGI_STOCK_H
#define __BENG_FCGI_STOCK_H

#include "pool.h"

struct fcgi_stock;

struct fcgi_stock *
fcgi_stock_new(pool_t pool);

void
fcgi_stock_kill(struct fcgi_stock *stock);

const char *
fcgi_stock_get(struct fcgi_stock *stock, const char *executable_path);

#endif
