/*
 * Generic connection balancer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef GENERIC_BALANCER_HXX
#define GENERIC_BALANCER_HXX

#include "balancer.hxx"
#include "failure.hxx"
#include "address_list.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <utility>

template<class R>
struct BalancerRequest : R {
    struct pool &pool;

    struct balancer &balancer;

    const AddressList &address_list;

    struct async_operation_ref &async_ref;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    const unsigned session_sticky;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    SocketAddress current_address;

    template<typename... Args>
    BalancerRequest(struct pool &_pool,
                    struct balancer &_balancer,
                    const AddressList &_address_list,
                    struct async_operation_ref &_async_ref,
                    unsigned _session_sticky,
                    Args&&... args)
        :R(std::forward<Args>(args)...),
         pool(_pool), balancer(_balancer),
         address_list(_address_list),
         async_ref(_async_ref),
         session_sticky(_session_sticky),
         retries(CalculateRetries(address_list)) {}

    BalancerRequest(const BalancerRequest &) = delete;

    static unsigned CalculateRetries(const AddressList &address_list) {
        const unsigned size = address_list.GetSize();
        if (size <= 1)
            return 0;
        else if (size == 2)
            return 1;
        else if (size == 3)
            return 2;
        else
            return 3;
    }

    static constexpr BalancerRequest &Cast(R &r) {
        return (BalancerRequest &)r;
    }

    const SocketAddress &GetAddress() const {
        return current_address;
    }

    void Next() {
        const SocketAddress address =
            balancer_get(balancer, address_list, session_sticky);

        /* we need to copy this address because it may come from
           the balancer's cache, and the according cache item may
           be flushed at any time */
        const struct sockaddr *new_address = (const struct sockaddr *)
            p_memdup(&pool, address.GetAddress(), address.GetSize());
        current_address = { new_address, address.GetSize() };

        R::Send(pool, current_address, async_ref);
    }

    void Success() {
        failure_unset(current_address, FAILURE_FAILED);
    }

    bool Failure() {
        failure_add(current_address);

        if (retries-- > 0){
            /* try again, next address */
            Next();
            return true;
        } else
            /* give up */
            return false;
    }

    template<typename... Args>
    static void Start(struct pool &pool,
                      Args&&... args) {
        auto r = NewFromPool<BalancerRequest>(pool, pool,
                                              std::forward<Args>(args)...);
        r->Next();
    }
};

#endif
