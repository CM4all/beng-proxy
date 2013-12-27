/*
 * Casting utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CAST_HXX
#define CAST_HXX

#include <stddef.h>

/**
 * Offset the given pointer by the specified number of bytes.
 */
static void *
OffsetPointer(void *p, ptrdiff_t offset)
{
    return (char *)p + offset;
}

template<typename T, typename U>
static T *
OffsetCast(U *p, ptrdiff_t offset)
{
    return reinterpret_cast<T *>(OffsetPointer(p, offset));
}

/**
 *
 */
#define ContainerCast(p, container, attribute) \
    OffsetCast<container, decltype(((container*)nullptr)->attribute)>\
    ((p), -ptrdiff_t(offsetof(container, attribute)))

#endif
