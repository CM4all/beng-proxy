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

#include "lease.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/Compiler.h"

#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>

struct pool;
struct lease_ref;
class StockMap;
class StockGetHandler;
struct StockItem;
struct StockStats;

/**
 * A #StockMap wrapper which allows multiple clients to use one
 * #StockItem.
 */
class MultiStock {
    class Item
        : public boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {

        struct Lease final : ::Lease {
            static constexpr auto link_mode = boost::intrusive::normal_link;
            typedef boost::intrusive::link_mode<link_mode> LinkMode;
            typedef boost::intrusive::list_member_hook<LinkMode> SiblingsListHook;

            SiblingsListHook siblings;

            typedef boost::intrusive::member_hook<Lease,
                                                  Lease::SiblingsListHook,
                                                  &Lease::siblings> SiblingsListMemberHook;

            Item &item;

            Lease(Item &_item):item(_item) {}

            /* virtual methods from class Lease */
            void ReleaseLease(bool _reuse) noexcept override {
                item.DeleteLease(this, _reuse);
            }
        };

        StockItem &item;

        boost::intrusive::list<Lease, Lease::SiblingsListMemberHook,
                               boost::intrusive::constant_time_size<false>> leases;

        unsigned remaining_leases;

        bool reuse = true;

    public:
        Item(unsigned _max_leases, StockItem &_item)
            :item(_item), remaining_leases(_max_leases) {}

        Item(const Item &) = delete;
        Item &operator=(const Item &) = delete;

        ~Item();

        gcc_pure
        const char *GetKey() const;

        bool IsFull() const {
            return remaining_leases == 0;
        }

        bool CanUse() const {
            return reuse && !IsFull();
        }

        void Fade() {
            reuse = false;
        }

        template<typename P>
        void FadeIf(P &&predicate) {
            if (predicate(item))
                Fade();
        }

    private:
        Lease &AddLease() {
            Lease *lease = new Lease(*this);
            leases.push_front(*lease);
            --remaining_leases;
            return *lease;
        }

    public:
        void AddLease(StockGetHandler &handler,
                      struct lease_ref &lease_ref);

        StockItem *AddLease(struct lease_ref &lease_ref) {
            lease_ref.Set(AddLease());
            return &item;
        }

        void DeleteLease(Lease *lease, bool _reuse);

        class Compare {
            gcc_pure
            bool Less(const char *a, const char *b) const;

        public:
            gcc_pure
            bool operator()(const char *a, const Item &b) const {
                return Less(a, b.GetKey());
            }

            gcc_pure
            bool operator()(const Item &a, const char *b) const {
                return Less(a.GetKey(), b);
            }

            gcc_pure
            bool operator()(const Item &a, const Item &b) const {
                return Less(a.GetKey(), b.GetKey());
            }
        };
    };

    typedef boost::intrusive::multiset<Item,
                                       boost::intrusive::compare<Item::Compare>,
                                       boost::intrusive::constant_time_size<false>> ItemMap;
    ItemMap items;

    StockMap &hstock;

public:
    explicit MultiStock(StockMap &_hstock)
        :hstock(_hstock) {}

    MultiStock(const MultiStock &) = delete;
    MultiStock &operator=(const MultiStock &) = delete;

    /**
     * @see Stock::FadeAll()
     */
    void FadeAll() {
        for (auto &i : items)
            i.Fade();
    }

    /**
     * @see Stock::FadeIf()
     */
    template<typename P>
    void FadeIf(P &&predicate) {
        for (auto &i : items)
            i.FadeIf(predicate);
    }

    /**
     * Obtains an item from the stock without going through the
     * callback.  This requires a stock class which finishes the
     * StockClass::Create() method immediately.
     *
     * Throws exception on error.
     *
     * @param max_leases the maximum number of leases per stock_item
     */
    StockItem *GetNow(struct pool &caller_pool, const char *uri, void *info,
                      unsigned max_leases,
                      struct lease_ref &lease_ref);

private:
    Item &MakeItem(struct pool &caller_pool, const char *uri, void *info,
                   unsigned max_leases);
};

#endif
