/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_catch.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <assert.h>

struct istream_catch {
    struct istream output;
    struct istream *input;
    off_t available;

    GError *(*callback)(GError *error, void *ctx);
    void *callback_ctx;
};

static constexpr char space[] =
    "                                "
    "                                "
    "                                "
    "                                ";

static void
catch_send_whitespace(struct istream_catch *c)
{
    size_t length, nbytes;

    assert(c->input == nullptr);
    assert(c->available > 0);

    do {
        if (c->available >= (off_t)sizeof(space) - 1)
            length = sizeof(space) - 1;
        else
            length = (size_t)c->available;

        nbytes = istream_invoke_data(&c->output, space, length);
        if (nbytes == 0)
            return;

        c->available -= nbytes;
        if (nbytes < length)
            return;
    } while (c->available > 0);

    istream_deinit_eof(&c->output);
}


/*
 * istream handler
 *
 */

static size_t
catch_input_data(const void *data, size_t length, void *ctx)
{
    auto *c = (struct istream_catch *)ctx;

    size_t nbytes = istream_invoke_data(&c->output, data, length);
    if (nbytes > 0) {
        if ((off_t)nbytes < c->available)
            c->available -= (off_t)nbytes;
        else
            c->available = 0;
    }

    return nbytes;
}

static ssize_t
catch_input_direct(enum istream_direct type, int fd, size_t max_length,
                   void *ctx)
{
    auto *c = (struct istream_catch *)ctx;

    ssize_t nbytes = istream_invoke_direct(&c->output, type, fd, max_length);
    if (nbytes > 0) {
        if ((off_t)nbytes < c->available)
            c->available -= (off_t)nbytes;
        else
            c->available = 0;
    }

    return nbytes;
}

static void
catch_input_abort(GError *error, void *ctx)
{
    auto *c = (struct istream_catch *)ctx;

    error = c->callback(error, c->callback_ctx);
    if (error != nullptr) {
        /* forward error to our handler */
        istream_deinit_abort(&c->output, error);
        return;
    }

    /* the error has been handled by the callback, and he has disposed
       it */

    c->input = nullptr;

    if (c->available > 0) {
        /* according to a previous call to method "available", there
           is more data which we must provide - fill that with space
           characters */
        c->input = nullptr;
        catch_send_whitespace(c);
    } else
        istream_deinit_eof(&c->output);
}

static const struct istream_handler catch_input_handler = {
    .data = catch_input_data,
    .direct = catch_input_direct,
    .eof = istream_forward_eof,
    .abort = catch_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_catch *
istream_to_catch(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_catch::output);
}

static off_t
istream_catch_available(struct istream *istream, bool partial)
{
    struct istream_catch *c = istream_to_catch(istream);

    if (c->input != nullptr) {
        off_t available = istream_available(c->input, partial);
        if (available != (off_t)-1 && available > c->available)
            c->available = available;

        return available;
    } else
        return c->available;
}

static void
istream_catch_read(struct istream *istream)
{
    struct istream_catch *c = istream_to_catch(istream);

    if (c->input != nullptr) {
        istream_handler_set_direct(c->input, c->output.handler_direct);
        istream_read(c->input);
    } else
        catch_send_whitespace(c);
}

static void
istream_catch_close(struct istream *istream)
{
    struct istream_catch *c = istream_to_catch(istream);

    if (c->input != nullptr)
        istream_free_handler(&c->input);

    istream_deinit(&c->output);
}

static const struct istream_class istream_catch = {
    .available = istream_catch_available,
    .read = istream_catch_read,
    .close = istream_catch_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_catch_new(struct pool *pool, struct istream *input,
                  GError *(*callback)(GError *error, void *ctx), void *ctx)
{
    struct istream_catch *c = istream_new_macro(pool, catch);

    assert(input != nullptr);
    assert(!istream_has_handler(input));
    assert(callback != nullptr);

    istream_assign_handler(&c->input, input,
                           &catch_input_handler, c,
                           0);
    c->available = 0;
    c->callback = callback;
    c->callback_ctx = ctx;

    return istream_struct_cast(&c->output);
}
