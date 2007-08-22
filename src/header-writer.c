/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-writer.h"
#include "compiler.h"

#include <assert.h>
#include <string.h>

void
header_writer_init(struct header_writer *hw, fifo_buffer_t buffer,
                   strmap_t headers)
{
    assert(hw != NULL);
    assert(buffer != NULL);

    hw->buffer = buffer;
    hw->headers = headers;

    if (headers == NULL) {
        hw->next = NULL;
    } else {
        strmap_rewind(headers);
        hw->next = strmap_next(headers);
    }
}

ssize_t
header_writer_run(struct header_writer *hw)
{
    const struct pair *current = hw->next;
    char *dest;
    size_t max_length;
    size_t length = 0, key_length, value_length;

    assert(hw != NULL);

    if (hw->buffer == NULL)
        return 0;

    dest = fifo_buffer_write(hw->buffer, &max_length);
    /* we always want enough room for the trailing \r\n */
    if (unlikely(dest == NULL || max_length < 2))
        return -2;

    while (current != NULL) {
        key_length = strlen(current->key);
        value_length = strlen(current->value);

        if (unlikely(length + key_length + 2 + value_length + 2 + 2 > max_length))
            break;

        memcpy(dest + length, current->key, key_length);
        length += key_length;
        dest[length++] = ':';
        dest[length++] = ' ';
        memcpy(dest + length, current->value, value_length);
        length += value_length;
        dest[length++] = '\r';
        dest[length++] = '\n';

        current = strmap_next(hw->headers);
    }

    hw->next = current;
    if (current == NULL) {
        assert(length + 2 <= max_length);
        dest[length++] = '\r';
        dest[length++] = '\n';
    }

    if (length == 0)
        return -2;

    fifo_buffer_append(hw->buffer, length);

    if (current == NULL)
        hw->buffer = NULL;

    return (ssize_t)length;
}
