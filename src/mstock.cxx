/*
 * A wrapper for hstock/stock that allows multiple users of one
 * stock_item.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "mstock.h"
#include "hstock.h"
#include "stock.h"
#include "hashmap.h"
#include "pool.h"
#include "lease.h"

#include <daemon/log.h>

#include <map>
#include <list>
#include <string>

#include <assert.h>

struct mstock {};

class MultiStock : public mstock {
    class Domain;
    typedef std::map<std::string, Domain> DomainMap;

    class Domain {
        class Item;
        typedef std::list<Item> ItemList;

        class Item {
            struct Lease : list_head {
                static const struct lease lease;

                const ItemList::iterator item;

                Lease(ItemList::iterator _item):item(_item) {}

                void Release(bool reuse) {
                    item->DeleteLease(this, reuse);
                }

                static void Release(bool reuse, void *ctx) {
                    ((Lease *)ctx)->Release(reuse);
                }
            };

            DomainMap::iterator domain;

            stock_item &item;

            unsigned n_leases;
            list_head leases;

            bool reuse;

        public:
            Item(DomainMap::iterator _domain, stock_item &_item)
                :domain(_domain), item(_item), n_leases(0), reuse(true) {
                list_init(&leases);
            }

            Item(const Item &) = delete;

            ~Item() {
                assert(n_leases == 0);
                assert(list_empty(&leases));

                domain->second.Put(domain->first.c_str(), item, reuse);
            }

            bool IsFull() const {
                // TODO: dynamic limit
                return n_leases >= 4;
            }

            bool CanUse() const {
                return reuse && !IsFull();
            }

        private:
            Lease &AddLease(ItemList::iterator i) {
                assert(&*i == this);

                ++n_leases;

                Lease *lease = new Lease(i);
                list_add(lease, &leases);
                return *lease;
            }

        public:
            void AddLease(ItemList::iterator i,
                          const stock_get_handler &handler, void *ctx,
                          struct lease_ref &lease_ref) {
                assert(&*i == this);

                Lease &lease = AddLease(i);
                lease_ref_set(&lease_ref, &Lease::lease, &lease);

                handler.ready(&item, ctx);
            }

            stock_item *AddLease(ItemList::iterator i,
                                 struct lease_ref &lease_ref) {
                assert(&*i == this);

                Lease &lease = AddLease(i);
                lease_ref_set(&lease_ref, &Lease::lease, &lease);
                return &item;
            }

            void DeleteLease(Lease *lease, bool _reuse) {
                reuse &= _reuse;

                auto ii = lease->item;

                assert(n_leases > 0);
                list_remove(lease);
                delete lease;

                --n_leases;

                if (n_leases == 0)
                    domain->second.DeleteItem(ii);
            }
        };

        MultiStock &stock;

        struct pool *pool;

        ItemList items;

    public:
        Domain(MultiStock &_stock, struct pool *_pool)
            :stock(_stock), pool(_pool) {
        }

        Domain(Domain &&other)
            :stock(other.stock), pool(other.pool) {
            assert(other.items.empty());

            other.pool = nullptr;
        }

        Domain(const Domain &) = delete;

        ~Domain() {
            assert(items.empty());

            if (pool != nullptr)
                pool_unref(pool);
        }

        ItemList::iterator FindUsableItem() {
            for (auto i = items.begin(), end = items.end(); i != end; ++i)
                if (i->CanUse())
                    return i;

            return items.end();
        }

        ItemList::iterator AddItem(DomainMap::iterator di, stock_item &si) {
            assert(&di->second == this);

            items.emplace_front(di, si);
            return items.begin();
        }

        stock_item *GetNow(DomainMap::iterator di,
                           struct pool *caller_pool,
                           const char *uri, void *info,
                           struct lease_ref &lease_ref,
                           GError **error_r);

        void DeleteItem(ItemList::iterator &ii) {
            ii = items.erase(ii);
        }

        void Put(const char *uri, stock_item &item, bool reuse) {
            stock.Put(uri, item, reuse);
        }
    };

    struct pool *pool;

    DomainMap domains;

    struct hstock *hstock;

public:
    MultiStock(struct pool *_pool, struct hstock *_hstock)
        :pool(pool_new_libc(_pool, "mstock")),
         hstock(_hstock) {}

    MultiStock(const MultiStock &) = delete;

    ~MultiStock() {
        hstock_free(hstock);
        pool_unref(pool);
    }

    void AddStats(stock_stats &data) const {
        hstock_add_stats(hstock, &data);
    }

    stock_item *GetNow(struct pool *caller_pool, const char *uri, void *info,
                       struct lease_ref &lease_ref,
                       GError **error_r);

    void Put(const char *uri, stock_item &item, bool reuse) {
        hstock_put(hstock, uri, &item, !reuse);
    }
};

const lease MultiStock::Domain::Item::Lease::lease = {
    Release,
};

stock_item *
MultiStock::Domain::GetNow(DomainMap::iterator di,
                           struct pool *caller_pool,
                           const char *uri, void *info,
                           struct lease_ref &lease_ref,
                           GError **error_r)
{
    auto i = FindUsableItem();
    if (i == items.end()) {
        stock_item *item =
            hstock_get_now(stock.hstock, caller_pool, uri, info,
                           error_r);
        items.emplace_front(di, *item);
        i = items.begin();
    }

    return i->AddLease(i, lease_ref);
}

inline stock_item *
MultiStock::GetNow(struct pool *caller_pool, const char *uri, void *info,
                   struct lease_ref &lease_ref,
                   GError **error_r)
{
    struct pool *domain_pool = pool_new_libc(pool, "mstock_domain");
    auto di = domains.insert(std::make_pair(uri, Domain(*this, domain_pool)))
        .first;
    return di->second.GetNow(di, caller_pool, uri, info, lease_ref, error_r);
}

/*
 * constructor
 *
 */

struct mstock *
mstock_new(struct pool *pool, struct hstock *hstock)
{
    return new MultiStock(pool, hstock);
}

void
mstock_free(struct mstock *_m)
{
    MultiStock *m = (MultiStock *)_m;
    delete m;
}

void
mstock_add_stats(const struct mstock *_m, stock_stats *data)
{
    const MultiStock &m = *(const MultiStock *)_m;
    m.AddStats(*data);
}

struct stock_item *
mstock_get_now(struct mstock *_m, struct pool *caller_pool,
               const char *uri, void *info,
               struct lease_ref *lease_ref,
               GError **error_r)
{
    MultiStock &m = *(MultiStock *)_m;
    return m.GetNow(caller_pool, uri, info, *lease_ref, error_r);
}
