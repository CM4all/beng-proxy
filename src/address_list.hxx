/*
 * Store a URI along with a list of socket addresses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_LIST_HXX
#define BENG_PROXY_ADDRESS_LIST_HXX

#include "sticky.h"

#include <inline/compiler.h>

#include <stddef.h>
#include <assert.h>

struct pool;
struct sockaddr;

struct address_list {
    static constexpr size_t MAX_ADDRESSES = 16;

    enum sticky_mode sticky_mode;

    /** the number of addresses */
    unsigned size;

    struct address_envelope *addresses[MAX_ADDRESSES];

    void Init() {
        sticky_mode = STICKY_NONE;
        size = 0;
    }

    void CopyFrom(struct pool *pool, const struct address_list &src);

    void SetStickyMode(enum sticky_mode _sticky_mode) {
        sticky_mode = _sticky_mode;
    }

    constexpr
    bool IsEmpty() const {
        return size == 0;
    }

    /**
     * Is there no more than one address?
     */
    constexpr
    bool IsSingle() const {
        return size == 1;
    }

    /**
     * @return false if the list is full
     */
    bool Add(struct pool *pool, const struct sockaddr *address, size_t length);

    const struct address_envelope &operator[](unsigned n) const {
        assert(n < size);

        return *addresses[n];
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
