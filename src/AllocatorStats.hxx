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

    AllocatorStats &operator+=(const AllocatorStats other) {
        brutto_size += other.brutto_size;
        netto_size += other.netto_size;
        return *this;
    }

    constexpr AllocatorStats operator+(const AllocatorStats other) const {
        return { brutto_size + other.brutto_size,
                netto_size + other.netto_size };
    }
};

#endif
