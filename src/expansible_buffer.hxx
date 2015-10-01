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
struct expansible_buffer;
struct StringView;

/**
 * @param hard_limit the buffer will refuse to grow beyond this size
 */
struct expansible_buffer *
expansible_buffer_new(struct pool *pool, size_t initial_size,
                      size_t hard_limit);

void
expansible_buffer_reset(struct expansible_buffer *eb);

bool
expansible_buffer_is_empty(const struct expansible_buffer *eb);

size_t
expansible_buffer_length(const struct expansible_buffer *eb);

/**
 * @return NULL if the operation would exceed the hard limit
 */
void *
expansible_buffer_write(struct expansible_buffer *eb, size_t length);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_write_buffer(struct expansible_buffer *eb,
                               const void *p, size_t length);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_write_string(struct expansible_buffer *eb, const char *p);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_set(struct expansible_buffer *eb,
                      const void *p, size_t length);

/**
 * @return false if the operation would exceed the hard limit
 */
bool
expansible_buffer_set(struct expansible_buffer *eb, StringView p);

const void *
expansible_buffer_read(const struct expansible_buffer *eb, size_t *size_r);

const char *
expansible_buffer_read_string(struct expansible_buffer *eb);

StringView
expansible_buffer_read_string_view(const struct expansible_buffer *eb);

void *
expansible_buffer_dup(const struct expansible_buffer *eb, struct pool *pool);

char *
expansible_buffer_strdup(const struct expansible_buffer *eb,
                         struct pool *pool);

#endif
