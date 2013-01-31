/*
 * A buffer which grows automatically.  Compared to growing_buffer, it
 * is optimized to be read as one complete buffer, instead of many
 * smaller chunks.  Additionally, it can be reused.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "expansible-buffer.h"
#include "strref.h"
#include "pool.h"

#include <inline/poison.h>

#include <assert.h>
#include <string.h>

struct expansible_buffer {
    struct pool *pool;
    char *buffer;
    size_t max_size, size;
};

struct expansible_buffer *
expansible_buffer_new(struct pool *pool, size_t initial_size)
{
    assert(initial_size > 0);

    struct expansible_buffer *eb = p_malloc(pool, sizeof(*eb));

    eb->pool = pool;
    eb->buffer = p_malloc(pool, initial_size);
    eb->max_size = initial_size;
    eb->size = 0;

    return eb;
}

void
expansible_buffer_reset(struct expansible_buffer *eb)
{
    poison_undefined(eb->buffer, eb->max_size);

    eb->size = 0;
}

bool
expansible_buffer_is_empty(const struct expansible_buffer *eb)
{
    return eb->size == 0;
}

size_t
expansible_buffer_length(const struct expansible_buffer *eb)
{
    return eb->size;
}

static void
expansible_buffer_resize(struct expansible_buffer *eb, size_t max_size)
{
    assert(eb != NULL);
    assert(max_size > eb->max_size);

    char *buffer = p_malloc(eb->pool, max_size);
    memcpy(buffer, eb->buffer, eb->size);

    p_free(eb->pool, eb->buffer);

    eb->buffer = buffer;
    eb->max_size = max_size;
}

void *
expansible_buffer_write(struct expansible_buffer *eb, size_t length)
{
    size_t new_size = eb->size + length;
    if (new_size > eb->max_size)
        expansible_buffer_resize(eb, ((new_size - 1) | 0x3ff) + 1);

    char *dest = eb->buffer + eb->size;
    eb->size = new_size;

    return dest;
}

void
expansible_buffer_write_buffer(struct expansible_buffer *eb,
                               const void *p, size_t length)
{
    memcpy(expansible_buffer_write(eb, length), p, length);
}

void
expansible_buffer_write_string(struct expansible_buffer *eb, const char *p)
{
    expansible_buffer_write_buffer(eb, p, strlen(p));
}

void
expansible_buffer_set(struct expansible_buffer *eb,
                      const void *p, size_t length)
{
    if (length > eb->max_size)
        expansible_buffer_resize(eb, ((length - 1) | 0x3ff) + 1);

    eb->size = length;
    memcpy(eb->buffer, p, length);
}

void
expansible_buffer_set_strref(struct expansible_buffer *eb,
                             const struct strref *s)
{
    expansible_buffer_set(eb, s->data, s->length);
}

const void *
expansible_buffer_read(const struct expansible_buffer *eb, size_t *size_r)
{
    *size_r = eb->size;
    return eb->buffer;
}

const char *
expansible_buffer_read_string(struct expansible_buffer *eb)
{
    if (eb->size == 0 || eb->buffer[eb->size - 1] != 0)
        /* append a null terminator */
        expansible_buffer_write_buffer(eb, "\0", 1);

    /* the buffer is now a valid C string (assuming it doesn't contain
       any nulls */
    return eb->buffer;
}

void
expansible_buffer_read_strref(const struct expansible_buffer *eb,
                              struct strref *s)
{
    s->data = expansible_buffer_read(eb, &s->length);
}

void *
expansible_buffer_dup(const struct expansible_buffer *eb, struct pool *pool)
{
    return p_memdup(pool, eb->buffer, eb->size);
}

char *
expansible_buffer_strdup(const struct expansible_buffer *eb, struct pool *pool)
{
    char *p = p_malloc(pool, eb->size + 1);
    memcpy(p, eb->buffer, eb->size);
    p[eb->size] = 0;
    return p;
}
