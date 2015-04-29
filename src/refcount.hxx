/*
 * Reference counting API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef REFCOUNT_HXX
#define REFCOUNT_HXX

#include <glib.h>

#include <type_traits>

#include <assert.h>

struct RefCount {
    volatile gint value;

    void Init() {
        g_atomic_int_set(&value, 1);
    }

    void Get() {
        assert(g_atomic_int_get(&value) > 0);

        g_atomic_int_inc(&value);
    }

    /**
     * Decreases the reference counter, and returns true if the counter
     * has reached 0.
     */
    bool Put() {
        assert(value > 0);

        return g_atomic_int_dec_and_test(&value);
    }
};

static_assert(std::is_trivial<RefCount>::value, "type is not trivial");

#endif
