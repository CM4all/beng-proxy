/*
 * A buffer which grows automatically.  Compared to growing_buffer, it
 * is optimized to be read as one complete buffer, instead of many
 * smaller chunks.  Additionally, it can be reused.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "expansible_buffer.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

#include <inline/poison.h>

#include <assert.h>
#include <string.h>

struct ExpansibleBuffer {
    struct pool *pool;
    char *buffer;
    size_t hard_limit;
    size_t max_size, size;
};

ExpansibleBuffer *
expansible_buffer_new(struct pool *pool, size_t initial_size,
                      size_t hard_limit)
{
    assert(initial_size > 0);
    assert(hard_limit >= initial_size);

    auto eb = NewFromPool<ExpansibleBuffer>(*pool);

    eb->pool = pool;
    eb->buffer = (char *)p_malloc(pool, initial_size);
    eb->hard_limit = hard_limit;
    eb->max_size = initial_size;
    eb->size = 0;

    return eb;
}

void
expansible_buffer_reset(ExpansibleBuffer *eb)
{
    poison_undefined(eb->buffer, eb->max_size);

    eb->size = 0;
}

bool
expansible_buffer_is_empty(const ExpansibleBuffer *eb)
{
    return eb->size == 0;
}

size_t
expansible_buffer_length(const ExpansibleBuffer *eb)
{
    return eb->size;
}

static bool
expansible_buffer_resize(ExpansibleBuffer *eb, size_t max_size)
{
    assert(eb != nullptr);
    assert(max_size > eb->max_size);

    if (max_size > eb->hard_limit)
        return false;

    char *buffer = (char *)p_malloc(eb->pool, max_size);
    memcpy(buffer, eb->buffer, eb->size);

    p_free(eb->pool, eb->buffer);

    eb->buffer = buffer;
    eb->max_size = max_size;
    return true;
}

void *
expansible_buffer_write(ExpansibleBuffer *eb, size_t length)
{
    size_t new_size = eb->size + length;
    if (new_size > eb->max_size &&
        !expansible_buffer_resize(eb, ((new_size - 1) | 0x3ff) + 1))
        return nullptr;

    char *dest = eb->buffer + eb->size;
    eb->size = new_size;

    return dest;
}

bool
expansible_buffer_write_buffer(ExpansibleBuffer *eb,
                               const void *p, size_t length)
{
    void *q = expansible_buffer_write(eb, length);
    if (q == nullptr)
        return false;

    memcpy(q, p, length);
    return true;
}

bool
expansible_buffer_write_string(ExpansibleBuffer *eb, const char *p)
{
    return expansible_buffer_write_buffer(eb, p, strlen(p));
}

bool
expansible_buffer_set(ExpansibleBuffer *eb,
                      const void *p, size_t length)
{
    if (length > eb->max_size &&
        !expansible_buffer_resize(eb, ((length - 1) | 0x3ff) + 1))
        return false;

    eb->size = length;
    memcpy(eb->buffer, p, length);
    return true;
}

bool
expansible_buffer_set(ExpansibleBuffer *eb, StringView p)
{
    return expansible_buffer_set(eb, p.data, p.size);
}

const void *
expansible_buffer_read(const ExpansibleBuffer *eb, size_t *size_r)
{
    *size_r = eb->size;
    return eb->buffer;
}

const char *
expansible_buffer_read_string(ExpansibleBuffer *eb)
{
    if (eb->size == 0 || eb->buffer[eb->size - 1] != 0)
        /* append a null terminator */
        expansible_buffer_write_buffer(eb, "\0", 1);

    /* the buffer is now a valid C string (assuming it doesn't contain
       any nulls */
    return eb->buffer;
}

StringView
expansible_buffer_read_string_view(const ExpansibleBuffer *eb)
{
    return { (const char *)eb->buffer, eb->size };
}

void *
expansible_buffer_dup(const ExpansibleBuffer *eb, struct pool *pool)
{
    return p_memdup(pool, eb->buffer, eb->size);
}

char *
expansible_buffer_strdup(const ExpansibleBuffer *eb, struct pool *pool)
{
    char *p = (char *)p_malloc(pool, eb->size + 1);
    memcpy(p, eb->buffer, eb->size);
    p[eb->size] = 0;
    return p;
}
