/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DEFAULT_CHUNK_ALLOCATOR_HXX
#define BENG_PROXY_DEFAULT_CHUNK_ALLOCATOR_HXX

#include <utility>

template<typename T> struct WritableBuffer;
struct SliceArea;

class DefaultChunkAllocator {
    SliceArea *area = nullptr;

public:
    DefaultChunkAllocator() = default;
    DefaultChunkAllocator(DefaultChunkAllocator &&src)
        :area(src.area) {
        src.area = nullptr;
    }

    DefaultChunkAllocator &operator=(const DefaultChunkAllocator &) = delete;

#ifndef NDEBUG
    ~DefaultChunkAllocator();
#endif

    friend void swap(DefaultChunkAllocator &a, DefaultChunkAllocator &b) {
        using std::swap;
        swap(a.area, b.area);
    }

    WritableBuffer<void> Allocate();
    void Free(void *p);
};

#endif
