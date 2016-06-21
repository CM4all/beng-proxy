/*
 * Allocating struct ConstBuffer from distributed pool (dpool).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_DBUFFER_HXX
#define SHM_DBUFFER_HXX

#include "dpool.hxx"
#include "util/ConstBuffer.hxx"

template<typename T>
static inline ConstBuffer<T>
DupBuffer(struct dpool &p, ConstBuffer<T> src)
    throw(std::bad_alloc)
{
    if (src.IsNull())
        return ConstBuffer<T>::Null();

    if (src.IsEmpty())
        return ConstBuffer<T>::FromVoid({"", 0});

    ConstBuffer<void> src_v = src.ToVoid();
    ConstBuffer<void> dest_v(d_memdup(p, src_v.data, src_v.size), src_v.size);

    return ConstBuffer<T>::FromVoid(dest_v);
}

#endif
