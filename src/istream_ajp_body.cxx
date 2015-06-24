/*
 * This istream filter wraps data inside AJPv13 packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_ajp_body.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "ajp_protocol.hxx"
#include "direct.h"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>

struct AjpBodyIstream {
    struct istream output;
    struct istream *input;

    size_t requested, packet_remaining;

    gcc_packed struct {
        struct ajp_header header;
        uint16_t length;
    } header;
    size_t header_sent;
};

static void
ajp_body_start_packet(AjpBodyIstream *ab, size_t length)
{
    assert(ab->requested > 0);
    assert(length > 0);

    if (length > ab->requested)
        length = ab->requested;

    if (length > 8192 - sizeof(ab->header))
        /* limit packets to 8 kB - up to 65535 might be possible,
           but has never been tested */
        length = 8192 - sizeof(ab->header);

    ab->packet_remaining = length;
    ab->requested -= length;

    ab->header.header.a = 0x12;
    ab->header.header.b = 0x34;
    ab->header.header.length =
        ToBE16(ab->packet_remaining + sizeof(ab->header.length));
    ab->header.length = ToBE16(ab->packet_remaining);
    ab->header_sent = 0;
}

/**
 * Returns true if the header is complete.
 */
static bool
ajp_body_write_header(AjpBodyIstream *ab)
{
    size_t length, nbytes;
    const char *p;

    assert(ab->packet_remaining > 0);
    assert(ab->header_sent <= sizeof(ab->header));

    length = sizeof(ab->header) - ab->header_sent;
    if (length == 0)
        return true;

    p = (const char *)&ab->header;
    p += ab->header_sent;

    nbytes = istream_invoke_data(&ab->output, p, length);
    if (nbytes > 0)
        ab->header_sent += nbytes;

    return nbytes == length;
}

/**
 * Returns true if the caller may write the packet body.
 */
static bool
ajp_body_make_packet(AjpBodyIstream *ab, size_t length)
{
    if (ab->packet_remaining == 0) {
        if (ab->requested == 0)
            return false;

        ajp_body_start_packet(ab, length);
    }

    return ajp_body_write_header(ab);
}


/*
 * istream handler
 *
 */

static size_t
ajp_body_input_data(const void *data, size_t length, void *ctx)
{
    auto *ab = (AjpBodyIstream *)ctx;
    size_t nbytes;

    if (!ajp_body_make_packet(ab, length))
        return 0;

    if (length > ab->packet_remaining)
        length = ab->packet_remaining;

    nbytes = istream_invoke_data(&ab->output, data, length);
    if (nbytes > 0)
        ab->packet_remaining -= nbytes;

    return nbytes;
}

static ssize_t
ajp_body_input_direct(enum istream_direct type, int fd, size_t max_length,
                      void *ctx)
{
    auto *ab = (AjpBodyIstream *)ctx;

    if (ab->packet_remaining == 0) {
        if (ab->requested == 0)
            return ISTREAM_RESULT_BLOCKING;

        /* start a new packet, size determined by
           direct_available() */
        ssize_t available = direct_available(fd, type, max_length);
        if (available <= 0)
            return available;

        ajp_body_start_packet(ab, available);
    }

    pool_ref(ab->output.pool);

    if (!ajp_body_write_header(ab)) {
        ssize_t ret = ab->input != NULL
            ? ISTREAM_RESULT_BLOCKING : ISTREAM_RESULT_CLOSED;
        pool_unref(ab->output.pool);
        return ret;
    }

    pool_unref(ab->output.pool);

    if (max_length > ab->packet_remaining)
        max_length = ab->packet_remaining;

    ssize_t nbytes = istream_invoke_direct(&ab->output, type, fd, max_length);
    if (nbytes > 0)
        ab->packet_remaining -= nbytes;

    return nbytes;
}

static const struct istream_handler ajp_body_input_handler = {
    .data = ajp_body_input_data,
    .direct = ajp_body_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline AjpBodyIstream *
istream_to_ab(struct istream *istream)
{
    return &ContainerCast2(*istream, &AjpBodyIstream::output);
}

static off_t
istream_ajp_body_available(struct istream *istream, bool partial)
{
    AjpBodyIstream *ab = istream_to_ab(istream);

    if (!partial)
        return -1;

    return istream_available(ab->input, partial);
}

static void
istream_ajp_body_read(struct istream *istream)
{
    AjpBodyIstream *ab = istream_to_ab(istream);

    if (ab->packet_remaining > 0 && !ajp_body_write_header(ab))
        return;

    if (ab->packet_remaining == 0 && ab->requested > 0) {
        /* start a new packet, as large as possible */
        off_t available = istream_available(ab->input, true);
        if (available > 0)
            ajp_body_start_packet(ab, available);
    }

    istream_handler_set_direct(ab->input, ab->output.handler_direct);
    istream_read(ab->input);
}

static void
istream_ajp_body_close(struct istream *istream)
{
    AjpBodyIstream *ab = istream_to_ab(istream);

    istream_free_handler(&ab->input);
    istream_deinit(&ab->output);
}

static constexpr struct istream_class istream_ajp_body = {
    .available = istream_ajp_body_available,
    .read = istream_ajp_body_read,
    .close = istream_ajp_body_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_ajp_body_new(struct pool *pool, struct istream *input)
{
    assert(input != NULL);
    assert(!istream_has_handler(input));

    auto *ab = NewFromPool<AjpBodyIstream>(*pool);
    istream_init(&ab->output, &istream_ajp_body, pool);

    ab->requested = 0;
    ab->packet_remaining = 0;

    istream_assign_handler(&ab->input, input,
                           &ajp_body_input_handler, ab,
                           0);

    return &ab->output;
}

void
istream_ajp_body_request(struct istream *istream, size_t length)
{
    AjpBodyIstream *ab = istream_to_ab(istream);

    /* we're not checking if this becomes larger than the request body
       - although Tomcat should know better, it requests more and
       more */
    ab->requested += length;
}
