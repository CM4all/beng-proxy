/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_MULTI_STOCK_HXX
#define BENG_PROXY_MULTI_STOCK_HXX

#include "util/Compiler.h"

struct pool;
struct lease_ref;
class StockMap;
struct StockItem;
struct StockStats;
class MultiStock;

/*
 * A wrapper for #Stock that allows multiple users of one #StockItem.
 */
gcc_malloc
MultiStock *
mstock_new(StockMap &hstock);

void
mstock_free(MultiStock *mstock);

/**
 * Obtain statistics.
 */
gcc_pure
void
mstock_add_stats(const MultiStock &stock, StockStats &data);

/**
 * Obtains an item from the mstock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 *
 * Throws exception on error.
 *
 * @param max_leases the maximum number of leases per stock_item
 */
gcc_pure
StockItem *
mstock_get_now(MultiStock &mstock, struct pool &caller_pool,
               const char *uri, void *info, unsigned max_leases,
               struct lease_ref &lease_ref);

#endif
