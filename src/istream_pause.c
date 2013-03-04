/*
 * istream facade that ignores read() calls until it is resumed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_pause.h"
#include "istream-internal.h"

#include <assert.h>
#include <string.h>

struct istream_pause {
    struct istream output;
    struct istream *input;
    bool resumed;
};


/*
 * istream implementation
 *
 */

static off_t
istream_pause_available(struct istream *istream, bool partial)
{
    struct istream_pause *pause = (struct istream_pause *)istream;

    return istream_available(pause->input, partial);
}

static off_t
istream_pause_skip(struct istream *istream, off_t length)
{
    struct istream_pause *pause = (struct istream_pause *)istream;

    return istream_skip(pause->input, length);
}

static void
istream_pause_read(struct istream *istream)
{
    struct istream_pause *pause = (struct istream_pause *)istream;

    istream_handler_set_direct(pause->input,
                               pause->output.handler_direct);

    if (pause->resumed)
        istream_read(pause->input);
}

static int
istream_pause_as_fd(struct istream *istream)
{
    struct istream_pause *pause = (struct istream_pause *)istream;

    int fd = istream_as_fd(pause->input);
    if (fd >= 0)
        istream_deinit(&pause->output);

    return fd;
}

static void
istream_pause_close(struct istream *istream)
{
    struct istream_pause *pause = (struct istream_pause *)istream;

    istream_close(pause->input);
    istream_deinit(&pause->output);
}

static const struct istream_class istream_pause = {
    .available = istream_pause_available,
    .skip = istream_pause_skip,
    .read = istream_pause_read,
    .as_fd = istream_pause_as_fd,
    .close = istream_pause_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_pause_new(struct pool *pool, struct istream *input)
{
    struct istream_pause *pause = istream_new_macro(pool, pause);

    istream_assign_handler(&pause->input, input,
                           &istream_forward_handler, &pause->output,
                           pause->output.handler_direct);

    pause->input = input;
    pause->resumed = false;
    return &pause->output;

}

void
istream_pause_resume(struct istream *istream)
{
    assert(istream != NULL);
    assert(istream->cls == &istream_pause);

    struct istream_pause *pause = (struct istream_pause *)istream;

    pause->resumed = true;
}
