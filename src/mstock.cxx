/*
 * A wrapper for hstock/stock that allows multiple users of one
 * stock_item.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "mstock.h"
#include "hstock.hxx"
#include "stock.h"
#include "hashmap.h"
#include "pool.h"
#include "lease.h"

#include <daemon/log.h>

#include <boost/intrusive/list.hpp>

#include <map>
#include <string>

#include <assert.h>

struct mstock {};

class MultiStock : public mstock {
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

                static void Dispose(Lease *l) {
                    delete l;
                }
            };

            const DomainMap::iterator domain;

            const unsigned max_leases;

            stock_item &item;

            boost::intrusive::list<Lease, Lease::SiblingsListMemberHook,
                                   boost::intrusive::constant_time_size<true>> leases;

            bool reuse;

        public:
            Item(DomainMap::iterator _domain, unsigned _max_leases,
                 stock_item &_item)
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
            void AddLease(const stock_get_handler &handler, void *ctx,
                          struct lease_ref &lease_ref) {
                Lease &lease = AddLease();
                lease_ref_set(&lease_ref, &Lease::lease, &lease);

                handler.ready(&item, ctx);
            }

            stock_item *AddLease(struct lease_ref &lease_ref) {
                Lease &lease = AddLease();
                lease_ref_set(&lease_ref, &Lease::lease, &lease);
                return &item;
            }

            void DeleteLease(Lease *lease, bool _reuse) {
                reuse &= _reuse;

                assert(!leases.empty());
                leases.erase_and_dispose(leases.iterator_to(*lease),
                                         Lease::Dispose);

                if (leases.empty())
                    domain->second.DeleteItem(*this);
            }

            static void Dispose(Item *i) {
                delete i;
            }
        };

        MultiStock &stock;

        struct pool *pool;

        typedef boost::intrusive::list<Item,
                                       boost::intrusive::constant_time_size<false>> ItemList;
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

        Item *FindUsableItem() {
            for (auto &i : items)
                if (i.CanUse())
                    return &i;

            return nullptr;
        }

        Item &AddItem(DomainMap::iterator di,
                      unsigned max_leases,
                      stock_item &si) {
            assert(&di->second == this);

            Item *item = new Item(di, max_leases, si);
            items.push_front(*item);
            return *item;
        }

        stock_item *GetNow(DomainMap::iterator di,
                           struct pool *caller_pool,
                           const char *uri, void *info,
                           unsigned max_leases,
                           struct lease_ref &lease_ref,
                           GError **error_r);

        void DeleteItem(Item &i) {
            items.erase_and_dispose(items.iterator_to(i), Item::Dispose);
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
                       unsigned max_leases,
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
                           unsigned max_leases,
                           struct lease_ref &lease_ref,
                           GError **error_r)
{
    auto i = FindUsableItem();
    if (i == nullptr) {
        stock_item *item =
            hstock_get_now(stock.hstock, caller_pool, uri, info,
                           error_r);
        i = new Item(di, max_leases, *item);
        items.push_front(*i);
    }

    return i->AddLease(lease_ref);
}

inline stock_item *
MultiStock::GetNow(struct pool *caller_pool, const char *uri, void *info,
                   unsigned max_leases,
                   struct lease_ref &lease_ref,
                   GError **error_r)
{
    struct pool *domain_pool = pool_new_libc(pool, "mstock_domain");
    auto di = domains.insert(std::make_pair(uri, Domain(*this, domain_pool)))
        .first;
    return di->second.GetNow(di, caller_pool, uri, info, max_leases,
                             lease_ref, error_r);
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
               const char *uri, void *info, unsigned max_leases,
               struct lease_ref *lease_ref,
               GError **error_r)
{
    MultiStock &m = *(MultiStock *)_m;
    return m.GetNow(caller_pool, uri, info, max_leases,
                    *lease_ref, error_r);
}
