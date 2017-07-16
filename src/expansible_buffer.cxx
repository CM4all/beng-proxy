/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "expansible_buffer.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"
#include "util/Poison.hxx"

#include <assert.h>
#include <string.h>

ExpansibleBuffer::ExpansibleBuffer(struct pool &_pool,
                                   size_t initial_size, size_t _hard_limit)
    :pool(_pool),
     buffer((char *)p_malloc(&pool, initial_size)),
     hard_limit(_hard_limit),
     max_size(initial_size) {
    assert(initial_size > 0);
    assert(hard_limit >= initial_size);
}

void
ExpansibleBuffer::Clear()
{
    PoisonUndefined(buffer, max_size);

    size = 0;
}

bool
ExpansibleBuffer::Resize(size_t new_max_size)
{
    assert(new_max_size > max_size);

    if (new_max_size > hard_limit)
        return false;

    char *new_buffer = (char *)p_malloc(&pool, new_max_size);
    memcpy(new_buffer, buffer, size);

    p_free(&pool, buffer);

    buffer = new_buffer;
    max_size = new_max_size;
    return true;
}

void *
ExpansibleBuffer::Write(size_t length)
{
    size_t new_size = size + length;
    if (new_size > max_size &&
        !Resize(((new_size - 1) | 0x3ff) + 1))
        return nullptr;

    char *dest = buffer + size;
    size = new_size;

    return dest;
}

bool
ExpansibleBuffer::Write(const void *p, size_t length)
{
    void *q = Write(length);
    if (q == nullptr)
        return false;

    memcpy(q, p, length);
    return true;
}

bool
ExpansibleBuffer::Write(const char *p)
{
    return Write(p, strlen(p));
}

bool
ExpansibleBuffer::Set(const void *p, size_t new_size)
{
    if (new_size > max_size && !Resize(((new_size - 1) | 0x3ff) + 1))
        return false;

    size = new_size;
    memcpy(buffer, p, new_size);
    return true;
}

bool
ExpansibleBuffer::Set(StringView p)
{
    return Set(p.data, p.size);
}

ConstBuffer<void>
ExpansibleBuffer::Read() const
{
    return {buffer, size};
}

const char *
ExpansibleBuffer::ReadString()
{
    if (size == 0 || buffer[size - 1] != 0)
        /* append a null terminator */
        Write("\0", 1);

    /* the buffer is now a valid C string (assuming it doesn't contain
       any nulls */
    return buffer;
}

StringView
ExpansibleBuffer::ReadStringView() const
{
    return { (const char *)buffer, size };
}

void *
ExpansibleBuffer::Dup(struct pool &_pool) const
{
    return p_memdup(&_pool, buffer, size);
}

char *
ExpansibleBuffer::StringDup(struct pool &_pool) const
{
    char *p = (char *)p_malloc(&_pool, size + 1);
    memcpy(p, buffer, size);
    p[size] = 0;
    return p;
}
