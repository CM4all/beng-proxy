/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef USET_HXX
#define USET_HXX

#include "util/StaticArray.hxx"

/**
 * A small helper class which manages a small set of unsigned
 * integers.
 */
class uset {
    StaticArray<unsigned, 64> values;

public:
    /**
     * Adds the specified value.  Does nothing if the #uset is full.  Does
     * not check if the value already exists.
     */
    void Insert(unsigned value) {
        values.checked_append(value);
    }

    gcc_pure
    bool Contains(unsigned value) const {
        return values.contains(value);
    }

    /**
     * Checks if the value exists, and if not, adds it.
     */
    bool ContainsOrInsert(unsigned value) {
        if (Contains(value))
            return true;

        Insert(value);
        return false;
    }
};

#endif
