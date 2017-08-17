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

#include "MultiStock.hxx"
#include "MapStock.hxx"
#include "GetHandler.hxx"
#include "Item.hxx"
#include "pool.hxx"
#include "lease.hxx"
#include "util/DeleteDisposer.hxx"

#include <boost/intrusive/list.hpp>

#include <map>
#include <string>

#include <assert.h>

class MultiStock {
    class Domain;
    typedef std::map<std::string, Domain> DomainMap;

    class Domain {
        class Item
            : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

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
                void ReleaseLease(bool _reuse) override {
                    item.DeleteLease(this, _reuse);
                }
            };

            const DomainMap::iterator domain;

            const unsigned max_leases;

            StockItem &item;

            boost::intrusive::list<Lease, Lease::SiblingsListMemberHook,
                                   boost::intrusive::constant_time_size<true>> leases;

            bool reuse;

        public:
            Item(DomainMap::iterator _domain, unsigned _max_leases,
                 StockItem &_item)
                :domain(_domain), max_leases(_max_leases), item(_item),
                 reuse(true) {
            }

            Item(const Item &) = delete;

            ~Item() {
                assert(leases.empty());

                item.Put(!reuse);
            }

            bool IsFull() const {
                return leases.size() >= max_leases;
            }

            bool CanUse() const {
                return reuse && !IsFull();
            }

        private:
            Lease &AddLease() {
                Lease *lease = new Lease(*this);
                leases.push_front(*lease);
                return *lease;
            }

        public:
            void AddLease(StockGetHandler &handler,
                          struct lease_ref &lease_ref) {
                lease_ref.Set(AddLease());

                handler.OnStockItemReady(item);
            }

            StockItem *AddLease(struct lease_ref &lease_ref) {
                lease_ref.Set(AddLease());
                return &item;
            }

            void DeleteLease(Lease *lease, bool _reuse) {
                reuse &= _reuse;

                assert(!leases.empty());
                leases.erase_and_dispose(leases.iterator_to(*lease),
                                         DeleteDisposer());

                if (leases.empty())
                    domain->second.DeleteItem(*this);
            }
        };

        MultiStock &stock;

        typedef boost::intrusive::list<Item,
                                       boost::intrusive::constant_time_size<false>> ItemList;
        ItemList items;

    public:
        Domain(MultiStock &_stock)
            :stock(_stock) {
        }

        Domain(Domain &&other)
            :stock(other.stock) {
            assert(other.items.empty());
        }

        Domain(const Domain &) = delete;

        ~Domain() {
            assert(items.empty());
        }

        Item *FindUsableItem() {
            for (auto &i : items)
                if (i.CanUse())
                    return &i;

            return nullptr;
        }

        Item &AddItem(DomainMap::iterator di,
                      unsigned max_leases,
                      StockItem &si) {
            assert(&di->second == this);

            Item *item = new Item(di, max_leases, si);
            items.push_front(*item);
            return *item;
        }

        StockItem *GetNow(DomainMap::iterator di,
                          struct pool &caller_pool,
                          const char *uri, void *info,
                          unsigned max_leases,
                          struct lease_ref &lease_ref);

        void DeleteItem(Item &i) {
            items.erase_and_dispose(items.iterator_to(i), DeleteDisposer());
        }

        void Put(StockItem &item, bool reuse) {
            item.Put(reuse);
        }
    };

    DomainMap domains;

    StockMap &hstock;

public:
    explicit MultiStock(StockMap &_hstock)
        :hstock(_hstock) {}

    MultiStock(const MultiStock &) = delete;

    StockItem *GetNow(struct pool &caller_pool, const char *uri, void *info,
                      unsigned max_leases,
                      struct lease_ref &lease_ref);
};

StockItem *
MultiStock::Domain::GetNow(DomainMap::iterator di,
                           struct pool &caller_pool,
                           const char *uri, void *info,
                           unsigned max_leases,
                           struct lease_ref &lease_ref)
{
    auto i = FindUsableItem();
    if (i == nullptr) {
        auto *item = stock.hstock.GetNow(caller_pool, uri, info);
        i = new Item(di, max_leases, *item);
        items.push_front(*i);
    }

    return i->AddLease(lease_ref);
}

inline StockItem *
MultiStock::GetNow(struct pool &caller_pool, const char *uri, void *info,
                   unsigned max_leases,
                   struct lease_ref &lease_ref)
{
    auto di = domains.insert(std::make_pair(uri, Domain(*this))).first;
    return di->second.GetNow(di, caller_pool, uri, info, max_leases,
                             lease_ref);
}

/*
 * constructor
 *
 */

MultiStock *
mstock_new(StockMap &hstock)
{
    return new MultiStock(hstock);
}

void
mstock_free(MultiStock *m)
{
    delete m;
}

StockItem *
mstock_get_now(MultiStock &m, struct pool &caller_pool,
               const char *uri, void *info, unsigned max_leases,
               struct lease_ref &lease_ref)
{
    return m.GetNow(caller_pool, uri, info, max_leases,
                    lease_ref);
}
