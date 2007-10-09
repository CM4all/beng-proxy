/*
 * An istream facade which waits for its inner istream to appear.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>
#include <string.h>

struct istream_delayed {
    struct istream output;
    istream_t input;
    int input_eof;
    void (*abort_callback)(void *ctx);
    void *callback_ctx;
};


static void
delayed_close(struct istream_delayed *delayed)
{
    if (delayed->input != NULL) {
        assert(delayed->abort_callback == NULL);

        istream_close(delayed->input);
        assert(delayed->input == NULL);
    } else if (delayed->abort_callback != NULL) {
        void (*abort_callback)(void *ctx) = delayed->abort_callback;
        void *callback_ctx = delayed->callback_ctx;

        delayed->abort_callback = NULL;
        delayed->callback_ctx = NULL;

        abort_callback(callback_ctx);
    }

    istream_invoke_free(&delayed->output);
}


static size_t
delayed_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_delayed *delayed = ctx;

    return istream_invoke_data(&delayed->output, data, length);
}

static ssize_t
delayed_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_delayed *delayed = ctx;

    return istream_invoke_direct(&delayed->output, type, fd, max_length);
}

static void
delayed_input_eof(void *ctx)
{
    struct istream_delayed *delayed = ctx;

    istream_clear_unref_handler(&delayed->input);
    delayed->input_eof = 1;

    pool_ref(delayed->output.pool);
    istream_invoke_eof(&delayed->output);
    delayed_close(delayed);
    pool_unref(delayed->output.pool);
}

static void
delayed_input_free(void *ctx)
{
    struct istream_delayed *delayed = ctx;

    if (!delayed->input_eof && delayed->input != NULL) {
        /* abort the transfer */
        istream_clear_unref(&delayed->input);
        /* XXX */
    }
}

static const struct istream_handler delayed_input_handler = {
    .data = delayed_input_data,
    .direct = delayed_input_direct,
    .eof = delayed_input_eof,
    .free = delayed_input_free,
};

static inline struct istream_delayed *
istream_to_delayed(istream_t istream)
{
    return (struct istream_delayed *)(((char*)istream) - offsetof(struct istream_delayed, output));
}

static void
istream_delayed_read(istream_t istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input != NULL)
        istream_read(delayed->input);
}

static void
istream_delayed_close(istream_t istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    delayed_close(delayed);
}

static const struct istream istream_delayed = {
    .read = istream_delayed_read,
    .close = istream_delayed_close,
};

istream_t
istream_delayed_new(pool_t pool, void (*abort_callback)(void *ctx),
                    void *callback_ctx)
{
    struct istream_delayed *delayed;

    assert(abort_callback != NULL);

    delayed = p_malloc(pool, sizeof(*delayed));
    delayed->output = istream_delayed;
    delayed->output.pool = pool;
    delayed->input = NULL;
    delayed->input_eof = 0;
    delayed->abort_callback = abort_callback;
    delayed->callback_ctx = callback_ctx;

    return &delayed->output;
}

void
istream_delayed_set(istream_t i_delayed, istream_t input)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    assert(delayed->input == NULL);
    assert(input != NULL);
    assert(input->handler == NULL);

    delayed->abort_callback = NULL;
    delayed->callback_ctx = NULL;

    istream_assign_ref_handler(&delayed->input, input,
                               &delayed_input_handler, delayed,
                               delayed->output.handler_direct);

    if (delayed->output.handler == NULL) /* allow this special case here */
        return;

    istream_read(input);
}
