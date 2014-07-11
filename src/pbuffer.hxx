/*
 * Allocating struct ConstBuffer from memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PBUFFER_HXX
#define PBUFFER_HXX

#include "pool.hxx"
#include "util/ConstBuffer.hxx"

template<typename T>
static inline ConstBuffer<T>
DupBuffer(pool *p, ConstBuffer<T> src)
{
    if (src.IsNull())
        return ConstBuffer<T>::Null();

    if (src.IsEmpty())
        return ConstBuffer<T>::FromVoid({"", 0});

    ConstBuffer<void> src_v = src.ToVoid();
    ConstBuffer<void> dest_v(p_memdup(p, src_v.data, src_v.size), src_v.size);
    return ConstBuffer<T>::FromVoid(dest_v);
}

#endif
