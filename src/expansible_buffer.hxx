/*
 * A buffer which grows automatically.  Compared to growing_buffer, it
 * is optimized to be read as one complete buffer, instead of many
 * smaller chunks.  Additionally, it can be reused.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_EXPANSIBLE_BUFFER_HXX
#define BENG_EXPANSIBLE_BUFFER_HXX

#include <stddef.h>

struct pool;
struct ExpansibleBuffer;
struct StringView;

/**
 * @param hard_limit the buffer will refuse to grow beyond this size
 */
ExpansibleBuffer *
expansible_buffer_new(struct pool *pool, size_t initial_size,
                      size_t hard_limit);

void
expansible_buffer_reset(ExpansibleBuffer *eb);

bool
expansible_buffer_is_empty(const ExpansibleBuffer *eb);

size_t
expansible_buffer_length(const ExpansibleBuffer *eb);

/**
 * @return NULL if the operation would exceed the hard limit
 */
void *
expansible_buffer_write(ExpansibleBuffer *eb, size_t length);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_write_buffer(ExpansibleBuffer *eb,
                               const void *p, size_t length);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_write_string(ExpansibleBuffer *eb, const char *p);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_set(ExpansibleBuffer *eb,
                      const void *p, size_t length);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_set(ExpansibleBuffer *eb, StringView p);

const void *
expansible_buffer_read(const ExpansibleBuffer *eb, size_t *size_r);

const char *
expansible_buffer_read_string(ExpansibleBuffer *eb);

StringView
expansible_buffer_read_string_view(const ExpansibleBuffer *eb);

void *
expansible_buffer_dup(const ExpansibleBuffer *eb, struct pool *pool);

char *
expansible_buffer_strdup(const ExpansibleBuffer *eb,
                         struct pool *pool);

#endif
