/*
 * A wrapper for hstock/stock that allows multiple users of one
 * StockItem.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "mstock.hxx"
#include "hstock.hxx"
#include "stock.hxx"
#include "hashmap.hxx"
#include "pool.hxx"
#include "lease.hxx"
#include "util/DeleteDisposer.hxx"

#include <daemon/log.h>

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

            struct Lease {
                static constexpr auto link_mode = boost::intrusive::normal_link;
                typedef boost::intrusive::link_mode<link_mode> LinkMode;
                typedef boost::intrusive::list_member_hook<LinkMode> SiblingsListHook;

                SiblingsListHook siblings;

                typedef boost::intrusive::member_hook<Lease,
                                                      Lease::SiblingsListHook,
                                                      &Lease::siblings> SiblingsListMemberHook;

                static const struct lease lease;

                Item &item;

                Lease(Item &_item):item(_item) {}

                void Release(bool _reuse) {
                    item.DeleteLease(this, _reuse);
                }

                static void Release(bool reuse, void *ctx) {
                    ((Lease *)ctx)->Release(reuse);
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

                domain->second.Put(domain->first.c_str(), item, reuse);
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
                Lease &lease = AddLease();
                lease_ref.Set(Lease::lease, &lease);

                handler.OnStockItemReady(item);
            }

            StockItem *AddLease(struct lease_ref &lease_ref) {
                Lease &lease = AddLease();
                lease_ref.Set(Lease::lease, &lease);
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
                          struct lease_ref &lease_ref,
                          GError **error_r);

        void DeleteItem(Item &i) {
            items.erase_and_dispose(items.iterator_to(i), DeleteDisposer());
        }

        void Put(const char *uri, StockItem &item, bool reuse) {
            stock.Put(uri, item, reuse);
        }
    };

    DomainMap domains;

    StockMap &hstock;

public:
    explicit MultiStock(StockMap &_hstock)
        :hstock(_hstock) {}

    MultiStock(const MultiStock &) = delete;

    ~MultiStock() {
        hstock_free(&hstock);
    }

    void FadeAll() {
        hstock_fade_all(hstock);
    }

    void AddStats(StockStats &data) const {
        hstock_add_stats(hstock, data);
    }

    StockItem *GetNow(struct pool &caller_pool, const char *uri, void *info,
                      unsigned max_leases,
                      struct lease_ref &lease_ref,
                      GError **error_r);

    void Put(const char *uri, StockItem &item, bool reuse) {
        hstock_put(hstock, uri, item, !reuse);
    }
};

const lease MultiStock::Domain::Item::Lease::lease = {
    Release,
};

StockItem *
MultiStock::Domain::GetNow(DomainMap::iterator di,
                           struct pool &caller_pool,
                           const char *uri, void *info,
                           unsigned max_leases,
                           struct lease_ref &lease_ref,
                           GError **error_r)
{
    auto i = FindUsableItem();
    if (i == nullptr) {
        StockItem *item =
            hstock_get_now(stock.hstock, caller_pool, uri, info,
                           error_r);
        if (item == nullptr)
            return nullptr;

        i = new Item(di, max_leases, *item);
        items.push_front(*i);
    }

    return i->AddLease(lease_ref);
}

inline StockItem *
MultiStock::GetNow(struct pool &caller_pool, const char *uri, void *info,
                   unsigned max_leases,
                   struct lease_ref &lease_ref,
                   GError **error_r)
{
    auto di = domains.insert(std::make_pair(uri, Domain(*this))).first;
    return di->second.GetNow(di, caller_pool, uri, info, max_leases,
                             lease_ref, error_r);
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

void
mstock_fade_all(MultiStock &m)
{
    m.FadeAll();
}

void
mstock_add_stats(const MultiStock *m, StockStats *data)
{
    m->AddStats(*data);
}

StockItem *
mstock_get_now(MultiStock &m, struct pool &caller_pool,
               const char *uri, void *info, unsigned max_leases,
               struct lease_ref &lease_ref,
               GError **error_r)
{
    return m.GetNow(caller_pool, uri, info, max_leases,
                    lease_ref, error_r);
}
