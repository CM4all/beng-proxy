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

#ifndef BENG_PROXY_STOCK_ITEM_HXX
#define BENG_PROXY_STOCK_ITEM_HXX

#include "util/Compiler.h"

#include <boost/intrusive/list_hook.hpp>

#include <exception>

struct pool;
class Stock;
class StockGetHandler;

struct CreateStockItem {
    Stock &stock;
    StockGetHandler &handler;

    /**
     * Wrapper for Stock::GetName()
     */
    gcc_pure
    const char *GetStockName() const;

    /**
     * Announce that the creation of this item has failed.
     */
    void InvokeCreateError(std::exception_ptr ep);

    /**
     * Announce that the creation of this item has been aborted by the
     * caller.
     */
    void InvokeCreateAborted();
};

struct StockItem
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    Stock &stock;

    StockGetHandler &handler;

    /**
     * If true, then this object will never be reused.
     */
    bool fade = false;

    /**
     * Kludge: this flag is true if this item is idle and is not yet
     * in a "clean" state (e.g. a WAS process after STOP), and cannot
     * yet be reused.  It will be postponed until this flag is false
     * again.  TODO: replace this kludge.
     */
    bool unclean = false;

#ifndef NDEBUG
    bool is_idle = false;
#endif

    explicit StockItem(CreateStockItem c)
        :stock(c.stock), handler(c.handler) {}

    StockItem(const StockItem &) = delete;
    StockItem &operator=(const StockItem &) = delete;

    virtual ~StockItem();

    /**
     * Wrapper for Stock::GetName()
     */
    gcc_pure
    const char *GetStockName() const;

    /**
     * Return a busy item to the stock.  This is a wrapper for
     * Stock::Put().
     */
    void Put(bool destroy);

    /**
     * Prepare this item to be borrowed by a client.
     *
     * @return false when this item is defunct and shall be destroyed
     */
    virtual bool Borrow() = 0;

    /**
     * Return this borrowed item into the "idle" list.
     *
     * @return false when this item is defunct and shall not be reused
     * again; it will be destroyed by the caller
     */
    virtual bool Release() = 0;

    /**
     * Announce that the creation of this item has finished
     * successfully, and it is ready to be used.
     */
    void InvokeCreateSuccess();

    /**
     * Announce that the creation of this item has failed.
     */
    void InvokeCreateError(std::exception_ptr ep);

    /**
     * Announce that the creation of this item has been aborted by the
     * caller.
     */
    void InvokeCreateAborted();

    /**
     * Announce that the item has been disconnected by the peer while
     * it was idle.
     */
    void InvokeIdleDisconnect();
};

#endif
