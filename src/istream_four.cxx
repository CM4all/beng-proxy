/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_four.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <assert.h>

struct istream_four {
    struct istream output;
    struct istream *input;
};


/*
 * istream handler
 *
 */

static size_t
four_input_data(const void *data, size_t length, void *ctx)
{
    auto *four = (struct istream_four *)ctx;

    if (length > 4)
        length = 4;

    return istream_invoke_data(&four->output, data, length);
}

static ssize_t
four_input_direct(enum istream_direct type, int fd, size_t max_length,
                  void *ctx)
{
    auto *four = (struct istream_four *)ctx;

    if (max_length > 4)
        max_length = 4;

    return istream_invoke_direct(&four->output, type, fd, max_length);
}

static constexpr struct istream_handler four_input_handler = {
    .data = four_input_data,
    .direct = four_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_four *
istream_to_four(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_four::output);
}

static void
istream_four_read(struct istream *istream)
{
    struct istream_four *four = istream_to_four(istream);

    istream_handler_set_direct(four->input, four->output.handler_direct);

    istream_read(four->input);
}

static void
istream_four_close(struct istream *istream)
{
    struct istream_four *four = istream_to_four(istream);

    assert(four->input != nullptr);

    istream_close_handler(four->input);
    istream_deinit(&four->output);
}

static constexpr struct istream_class istream_four = {
    .read = istream_four_read,
    .close = istream_four_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_four_new(struct pool *pool, struct istream *input)
{
    struct istream_four *four = istream_new_macro(pool, four);

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    istream_assign_handler(&four->input, input,
                           &four_input_handler, four,
                           0);

    return istream_struct_cast(&four->output);
}
