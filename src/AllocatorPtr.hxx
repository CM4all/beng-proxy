/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ALLOCATOR_PTR_HXX
#define BENG_PROXY_ALLOCATOR_PTR_HXX

#include "pool.hxx"
#include "util/StringView.hxx"

#include <string.h>

struct StringView;
template<typename T> struct ConstBuffer;

class AllocatorPtr {
    struct pool &pool;

public:
    AllocatorPtr(struct pool &_pool):pool(_pool) {}

    char *Dup(const char *src) {
        return p_strdup(&pool, src);
    }

    const char *CheckDup(const char *src) {
        return p_strdup_checked(&pool, src);
    }

    template<typename... Args>
    char *Concat(Args&&... args) {
        const size_t length = ConcatLength(args...);
        char *result = NewArray<char>(length + 1);
        *ConcatCopy(result, args...) = 0;
        return result;
    }

    template<typename T, typename... Args>
    T *New(Args&&... args) {
        return NewFromPool<T>(pool, std::forward<Args>(args)...);
    }

    template<typename T>
    T *NewArray(size_t n) {
        return PoolAlloc<T>(pool, n);
    }

    void *Dup(const void *data, size_t size) {
        return p_memdup(&pool, data, size);
    }

    ConstBuffer<void> Dup(ConstBuffer<void> src);
    StringView Dup(StringView src);
    const char *DupZ(StringView src);

private:
    template<typename... Args>
    static size_t ConcatLength(const char *s, Args... args) {
        return strlen(s) + ConcatLength(args...);
    }

    template<typename... Args>
    static constexpr size_t ConcatLength(StringView s, Args... args) {
        return s.size + ConcatLength(args...);
    }

    static constexpr size_t ConcatLength() {
        return 0;
    }

    template<typename... Args>
    static char *ConcatCopy(char *p, const char *s, Args... args) {
        return ConcatCopy(stpcpy(p, s), args...);
    }

    template<typename... Args>
    static char *ConcatCopy(char *p, StringView s, Args... args) {
        return ConcatCopy((char *)mempcpy(p, s.data, s.size), args...);
    }

    template<typename... Args>
    static char *ConcatCopy(char *p) {
        return p;
    }
};

#endif
