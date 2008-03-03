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
    size_t rest;
};


/*
 * istream handler
 *
 */

static size_t
head_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_head *head = ctx;
    size_t nbytes;

    if (head->rest == 0)
        return 0;

    if (length > head->rest)
        length = head->rest;

    nbytes = istream_invoke_data(&head->output, data, length);
    assert(nbytes <= head->rest);

    if (nbytes > 0) {
        head->rest -= nbytes;
        if (head->rest == 0) {
            istream_free_unref_handler(&head->input);
            istream_deinit_eof(&head->output);
            return 0;
        }
    }

    return nbytes;
}

static ssize_t
head_source_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_head *head = ctx;
    ssize_t nbytes;

    if (head->rest == 0)
        return -2;

    if (max_length > head->rest)
        max_length = head->rest;

    nbytes = istream_invoke_direct(&head->output, type, fd, max_length);
    assert(nbytes < 0 || (size_t)nbytes <= head->rest);

    if (nbytes > 0) {
        head->rest -= (size_t)nbytes;
        if (head->rest == 0) {
            istream_free_unref_handler(&head->input);
            istream_deinit_eof(&head->output);
        }
    }

    return nbytes;
}

static void
head_source_eof(void *ctx)
{
    struct istream_head *head = ctx;

    istream_clear_unref(&head->input);
    istream_deinit_eof(&head->output);
}

static void
head_source_abort(void *ctx)
{
    struct istream_head *head = ctx;

    istream_clear_unref(&head->input);
    istream_deinit_abort(&head->output);
}

static const struct istream_handler head_input_handler = {
    .data = head_source_data,
    .direct = head_source_direct,
    .eof = head_source_eof,
    .abort = head_source_abort,
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

static void
istream_head_read(istream_t istream)
{
    struct istream_head *head = istream_to_head(istream);

    if (head->rest == 0) {
        istream_free_unref_handler(&head->input);
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

    istream_free_unref(&head->input);
}

static const struct istream istream_head = {
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

    istream_assign_ref_handler(&head->input, input,
                               &head_input_handler, head,
                               0);

    head->rest = size;

    return istream_struct_cast(&head->output);
}
