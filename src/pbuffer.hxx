/*
 * Allocating struct ConstBuffer from memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PBUFFER_HXX
#define PBUFFER_HXX

#include "pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringView.hxx"

template<typename T>
static inline ConstBuffer<T>
DupBuffer(pool &p, ConstBuffer<T> src)
{
    if (src.IsNull())
        return ConstBuffer<T>::Null();

    if (src.IsEmpty())
        return ConstBuffer<T>::FromVoid({"", 0});

    ConstBuffer<void> src_v = src.ToVoid();
    ConstBuffer<void> dest_v(p_memdup(&p, src_v.data, src_v.size), src_v.size);
    return ConstBuffer<T>::FromVoid(dest_v);
}

static inline StringView
DupBuffer(pool &p, StringView src)
{
    if (src.IsNull())
        return nullptr;

    if (src.IsEmpty())
        return StringView::Empty();

    return StringView((const char *)p_memdup(&p, src.data, src.size),
                      src.size);
}

/**
 * Allocate a new buffer with data concatenated from the given source
 * buffers.  If one is empty, this may return a pointer to the other
 * buffer.
 */
ConstBuffer<void>
LazyCatBuffer(struct pool &pool, ConstBuffer<void> a, ConstBuffer<void> b);

#endif
