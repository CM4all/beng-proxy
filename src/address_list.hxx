/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_LIST_HXX
#define BENG_PROXY_ADDRESS_LIST_HXX

#include "net/SocketAddress.hxx"
#include "util/StaticArray.hxx"
#include "StickyMode.hxx"

#include <inline/compiler.h>

#include <stddef.h>
#include <assert.h>

struct pool;
class SocketAddress;

struct AddressList {
    static constexpr size_t MAX_ADDRESSES = 16;

    StickyMode sticky_mode = StickyMode::NONE;

    typedef StaticArray<SocketAddress, MAX_ADDRESSES> Array;
    typedef Array::const_iterator const_iterator;

    Array addresses;

    AddressList() = default;
    AddressList(struct pool &pool, const AddressList &src);

    void CopyFrom(struct pool *pool, const AddressList &src);

    void SetStickyMode(StickyMode _sticky_mode) {
        sticky_mode = _sticky_mode;
    }

    constexpr
    bool IsEmpty() const {
        return addresses.empty();
    }

    Array::size_type GetSize() const {
        return addresses.size();
    }

    /**
     * Is there no more than one address?
     */
    constexpr
    bool IsSingle() const {
        return addresses.size() == 1;
    }

    constexpr const_iterator begin() const {
        return addresses.begin();
    }

    constexpr const_iterator end() const {
        return addresses.end();
    }

    /**
     * @return false if the list is full
     */
    bool Add(struct pool *pool, SocketAddress address);

    const SocketAddress &operator[](unsigned n) const {
        assert(addresses[n].IsDefined());

        return addresses[n];
    }

    gcc_pure
    const SocketAddress *GetFirst() const;

    /**
     * Generates a unique string which identifies this object in a hash
     * table.  This string stored in a statically allocated buffer.
     */
    gcc_pure
    const char *GetKey() const;
};

#endif
