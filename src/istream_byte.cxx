/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_byte.hxx"
#include "istream_pointer.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "util/Cast.hxx"
#include "pool.hxx"

#include <assert.h>

struct ByteIstream {
    struct istream output;
    IstreamPointer input;

    ByteIstream(struct pool &p, struct istream &_input);
};


/*
 * istream handler
 *
 */

static size_t
byte_input_data(const void *data, gcc_unused size_t length, void *ctx)
{
    auto *byte = (ByteIstream *)ctx;

    return istream_invoke_data(&byte->output, data, 1);
}

static ssize_t
byte_input_direct(enum istream_direct type, int fd,
                  gcc_unused size_t max_length, void *ctx)
{
    auto *byte = (ByteIstream *)ctx;

    return istream_invoke_direct(&byte->output, type, fd, 1);
}

static const struct istream_handler byte_input_handler = {
    .data = byte_input_data,
    .direct = byte_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline ByteIstream *
istream_to_byte(struct istream *istream)
{
    return &ContainerCast2(*istream, &ByteIstream::output);
}

static void
istream_byte_read(struct istream *istream)
{
    ByteIstream *byte = istream_to_byte(istream);

    byte->input.SetDirect(byte->output.handler_direct);
    byte->input.Read();
}

static void
istream_byte_close(struct istream *istream)
{
    ByteIstream *byte = istream_to_byte(istream);

    assert(byte->input.IsDefined());

    byte->input.CloseHandler();
    istream_deinit(&byte->output);
}

static const struct istream_class istream_byte = {
    .read = istream_byte_read,
    .close = istream_byte_close,
};


/*
 * constructor
 *
 */

inline
ByteIstream::ByteIstream(struct pool &p, struct istream &_input)
    :output(p, istream_byte),
     input(_input, byte_input_handler, this) {}

struct istream *
istream_byte_new(struct pool &pool, struct istream &input)
{
    auto *byte = NewFromPool<ByteIstream>(pool, pool, input);
    return &byte->output;
}
