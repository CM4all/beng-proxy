/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_head.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "util/Cast.hxx"

#include <assert.h>
#include <string.h>

struct HeadIstream {
    struct istream output;
    struct istream *input;
    off_t rest;
    bool authoritative;
};

/*
 * istream handler
 *
 */

static size_t
head_input_data(const void *data, size_t length, void *ctx)
{
    HeadIstream *head = (HeadIstream *)ctx;

    if (head->rest == 0) {
        istream_close_handler(head->input);
        istream_deinit_eof(&head->output);
        return 0;
    }

    if ((off_t)length > head->rest)
        length = head->rest;

    size_t nbytes = istream_invoke_data(&head->output, data, length);
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
head_input_direct(enum istream_direct type, int fd, size_t max_length,
                  void *ctx)
{
    HeadIstream *head = (HeadIstream *)ctx;

    if (head->rest == 0) {
        istream_close_handler(head->input);
        istream_deinit_eof(&head->output);
        return ISTREAM_RESULT_CLOSED;
    }

    if ((off_t)max_length > head->rest)
        max_length = head->rest;

    ssize_t nbytes = istream_invoke_direct(&head->output, type, fd,
                                           max_length);
    assert(nbytes < 0 || (off_t)nbytes <= head->rest);

    if (nbytes > 0) {
        head->rest -= (size_t)nbytes;
        if (head->rest == 0) {
            istream_close_handler(head->input);
            istream_deinit_eof(&head->output);
            return ISTREAM_RESULT_CLOSED;
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

static inline HeadIstream *
istream_to_head(struct istream *istream)
{
    return &ContainerCast2(*istream, &HeadIstream::output);
}

static off_t
istream_head_available(gcc_unused struct istream *istream, bool partial)
{
    HeadIstream *head = istream_to_head(istream);
    if (head->authoritative) {
        assert(partial ||
               istream_available(head->input, partial) < 0 ||
               istream_available(head->input, partial) >= (off_t)head->rest);
        return head->rest;
    }

    off_t available = istream_available(head->input, partial);

    if (available > (off_t)head->rest)
        available = head->rest;

    return available;
}

static off_t
istream_head_skip(struct istream *istream, off_t length)
{
    HeadIstream *head = istream_to_head(istream);

    if (length >= head->rest)
        length = head->rest;

    off_t nbytes = istream_skip(head->input, length);
    assert(nbytes <= length);

    if (nbytes > 0)
        head->rest -= nbytes;

    return nbytes;
}

static void
istream_head_read(struct istream *istream)
{
    HeadIstream *head = istream_to_head(istream);

    if (head->rest == 0) {
        istream_close_handler(head->input);
        istream_deinit_eof(&head->output);
    } else {
        istream_handler_set_direct(head->input, head->output.handler_direct);

        istream_read(head->input);
    }
}

static void
istream_head_close(struct istream *istream)
{
    HeadIstream *head = istream_to_head(istream);

    istream_close_handler(head->input);
    istream_deinit(&head->output);
}

static const struct istream_class istream_head = {
    .available = istream_head_available,
    .skip = istream_head_skip,
    .read = istream_head_read,
    .close = istream_head_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_head_new(struct pool *pool, struct istream *input,
                 size_t size, bool authoritative)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto head = NewFromPool<HeadIstream>(*pool);
    istream_init(&head->output, &istream_head, pool);

    istream_assign_handler(&head->input, input,
                           &head_input_handler, head,
                           0);

    head->rest = size;
    head->authoritative = authoritative;

    return &head->output;
}
