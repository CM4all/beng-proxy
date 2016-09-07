/*
 * Reference counting API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef REFCOUNT_HXX
#define REFCOUNT_HXX

#include <atomic>

class RefCount {
    std::atomic_uint value;

public:
    constexpr RefCount():value(1) {}

    void Get() {
        ++value;
    }

    /**
     * Decreases the reference counter, and returns true if the counter
     * has reached 0.
     */
    bool Put() {
        return --value == 0;
    }
};

#endif
