/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_LIST_HXX
#define BENG_PROXY_ADDRESS_LIST_HXX

#include "util/TrivialArray.hxx"
#include "util/DereferenceIterator.hxx"
#include "sticky.h"

#include <inline/compiler.h>

#include <stddef.h>
#include <assert.h>

struct pool;
class SocketAddress;

struct AddressList {
    static constexpr size_t MAX_ADDRESSES = 16;

    enum sticky_mode sticky_mode;

    typedef TrivialArray<struct address_envelope *, MAX_ADDRESSES> Array;
    typedef DereferenceIterator<Array::const_iterator,
                                struct address_envelope> const_iterator;

    Array addresses;

    void Init() {
        sticky_mode = STICKY_NONE;
        addresses.clear();
    }

    void CopyFrom(struct pool *pool, const AddressList &src);

    void SetStickyMode(enum sticky_mode _sticky_mode) {
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

    const struct address_envelope &operator[](unsigned n) const {
        const struct address_envelope *envelope = addresses[n];
        assert(envelope != nullptr);
        return *envelope;
    }

    gcc_pure
    const struct address_envelope *GetFirst() const;

    /**
     * Generates a unique string which identifies this object in a hash
     * table.  This string stored in a statically allocated buffer.
     */
    gcc_pure
    const char *GetKey() const;
};

#endif
