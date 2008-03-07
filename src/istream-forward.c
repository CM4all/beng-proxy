/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

size_t
istream_forward_data(const void *data, size_t length, void *ctx)
{
    struct istream *istream = ctx;
    return istream_invoke_data(istream, data, length);
}

ssize_t
istream_forward_direct(istream_direct_t type, int fd, size_t max_length,
                       void *ctx)
{
    struct istream *istream = ctx;
    return istream_invoke_direct(istream, type, fd, max_length);
}

void
istream_forward_eof(void *ctx)
{
    struct istream *istream = ctx;
    istream_deinit_eof(istream);
}

void
istream_forward_abort(void *ctx)
{
    struct istream *istream = ctx;
    istream_deinit_abort(istream);
}
