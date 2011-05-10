/*
 * This istream filter passes only the first N bytes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>
#include <string.h>

struct istream_head {
    struct istream output;
    istream_t input;
    off_t rest;
};


/*
 * istream handler
 *
 */

static size_t
head_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_head *head = ctx;
    size_t nbytes;

    if (head->rest == 0) {
        istream_close_handler(head->input);
        istream_deinit_eof(&head->output);
        return 0;
    }

    if ((off_t)length > head->rest)
        length = head->rest;

    nbytes = istream_invoke_data(&head->output, data, length);
    assert((off_t)nbytes <= head->rest);

    if (nbytes > 0) {
        head->rest -= nbytes;
        if (head->rest == 0) {
            istream_close_handler(head->input);
            istream_deinit_eof(&head->output);
            return 0;
        }
    }

    return nbytes;
}

static ssize_t
head_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_head *head = ctx;
    ssize_t nbytes;

    if (head->rest == 0) {
        istream_close_handler(head->input);
        istream_deinit_eof(&head->output);
        return -3;
    }

    if ((off_t)max_length > head->rest)
        max_length = head->rest;

    nbytes = istream_invoke_direct(&head->output, type, fd, max_length);
    assert(nbytes < 0 || (off_t)nbytes <= head->rest);

    if (nbytes > 0) {
        head->rest -= (size_t)nbytes;
        if (head->rest == 0) {
            istream_close_handler(head->input);
            istream_deinit_eof(&head->output);
            return -3;
        }
    }

    return nbytes;
}

static const struct istream_handler head_input_handler = {
    .data = head_input_data,
    .direct = head_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_head *
istream_to_head(istream_t istream)
{
    return (struct istream_head *)(((char*)istream) - offsetof(struct istream_head, output));
}

static off_t
istream_head_available(__attr_unused istream_t istream, bool partial)
{
    struct istream_head *head = istream_to_head(istream);
    off_t available = istream_available(head->input, partial);

    if (available > (off_t)head->rest)
        available = head->rest;

    return available;
}

static void
istream_head_read(istream_t istream)
{
    struct istream_head *head = istream_to_head(istream);

    if (head->rest == 0) {
        istream_close_handler(head->input);
        istream_deinit_eof(&head->output);
    } else {
        istream_handler_set_direct(head->input, head->output.handler_direct);

        istream_read(head->input);
    }
}

static void
istream_head_close(istream_t istream)
{
    struct istream_head *head = istream_to_head(istream);

    istream_close_handler(head->input);
    istream_deinit_abort(&head->output);
}

static const struct istream istream_head = {
    .available = istream_head_available,
    .read = istream_head_read,
    .close = istream_head_close,
};


/*
 * constructor
 *
 */

istream_t
istream_head_new(pool_t pool, istream_t input, size_t size)
{
    struct istream_head *head = istream_new_macro(pool, head);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&head->input, input,
                           &head_input_handler, head,
                           0);

    head->rest = size;

    return istream_struct_cast(&head->output);
}
