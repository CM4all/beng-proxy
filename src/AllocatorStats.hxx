/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ALLOCATOR_STATS_HXX
#define BENG_PROXY_ALLOCATOR_STATS_HXX

struct AllocatorStats {
    /**
     * Number of bytes allocated from the kernel.
     */
    size_t brutto_size;

    /**
     * Number of bytes being used by client code.
     */
    size_t netto_size;

    static constexpr AllocatorStats Zero() {
        return { 0, 0 };
    }

    void Clear() {
        brutto_size = 0;
        netto_size = 0;
    }
};

#endif
