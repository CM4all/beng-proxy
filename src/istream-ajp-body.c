/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "ajp-protocol.h"

#include <assert.h>
#include <netinet/in.h>

struct istream_ajp_body {
    struct istream output;
    istream_t input;

    size_t requested, packet_remaining;

    __attr_packed struct {
        struct ajp_header header;
        uint16_t length;
    } header;
    size_t header_sent;
};

static void
ajp_body_start_packet(struct istream_ajp_body *ab)
{
    assert(ab->requested > 0);

    ab->packet_remaining = ab->requested;
    if (ab->packet_remaining > 8192)
        /* limit packets to 8 kB - up to 65535 might be possible,
           but has never been tested */
        ab->packet_remaining = 8192;

    ab->requested -= ab->packet_remaining;

    ab->header.header.a = 0x12;
    ab->header.header.b = 0x34;
    ab->header.header.length =
        htons(ab->packet_remaining + sizeof(ab->header.length));
    ab->header.length = htons(ab->packet_remaining);
    ab->header_sent = 0;
}

/**
 * Returns true if the header is complete.
 */
static bool
ajp_body_write_header(struct istream_ajp_body *ab)
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
 * Returns true if the packet is complete.
 */
/*
static bool
ajp_body_write_data(struct istream_ajp_body *ab)
{
    bool ret;

    pool_ref(ab->output.pool);

    istream_read(ab->input);
    ret = ab->packet_remaining == 0;

    pool_unref(ab->output.pool);

    return ret;
}
*/

/**
 * Returns true if the caller may write the packet body.
 */
static bool
ajp_body_make_packet(struct istream_ajp_body *ab)
{
    if (ab->packet_remaining == 0) {
        if (ab->requested == 0)
            return false;

        ajp_body_start_packet(ab);
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
    struct istream_ajp_body *ab = ctx;
    size_t nbytes;

    if (!ajp_body_make_packet(ab))
        return 0;

    if (length > ab->packet_remaining)
        length = ab->packet_remaining;

    nbytes = istream_invoke_data(&ab->output, data, length);
    if (nbytes > 0)
        ab->packet_remaining -= nbytes;

    return nbytes;
}

static const struct istream_handler ajp_body_input_handler = {
    .data = ajp_body_input_data,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_ajp_body *
istream_to_ab(istream_t istream)
{
    return (struct istream_ajp_body *)(((char*)istream) - offsetof(struct istream_ajp_body, output));
}

static off_t
istream_ajp_body_available(istream_t istream, bool partial)
{
    struct istream_ajp_body *ab = istream_to_ab(istream);

    if (!partial)
        return -1;

    return istream_available(ab->input, partial);
}

static void
istream_ajp_body_read(istream_t istream)
{
    struct istream_ajp_body *ab = istream_to_ab(istream);

    if (ab->packet_remaining > 0 && !ajp_body_write_header(ab))
        return;

    istream_read(ab->input);
}

static void
istream_ajp_body_close(istream_t istream)
{
    struct istream_ajp_body *ab = istream_to_ab(istream);

    istream_free_handler(&ab->input);
    istream_deinit_abort(&ab->output);
}

static const struct istream istream_ajp_body = {
    .available = istream_ajp_body_available,
    .read = istream_ajp_body_read,
    .close = istream_ajp_body_close,
};


/*
 * constructor
 *
 */

istream_t
istream_ajp_body_new(pool_t pool, istream_t input)
{
    struct istream_ajp_body *ab = istream_new_macro(pool, ajp_body);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    ab->requested = 0;
    ab->packet_remaining = 0;

    istream_assign_handler(&ab->input, input,
                           &ajp_body_input_handler, ab,
                           0);

    return istream_struct_cast(&ab->output);
}

void
istream_ajp_body_request(istream_t istream, size_t length)
{
    struct istream_ajp_body *ab = istream_to_ab(istream);
    off_t available = istream_available(ab->input, false);

    assert(available == -1 ||
           (off_t)ab->requested + (off_t)ab->packet_remaining <= available);

    ab->requested += length;

    if (available != -1 &&
        (off_t)ab->requested + (off_t)ab->packet_remaining > available) {
        /* GET_BODY_CHUNK packet was too large - abort this stream */
        istream_free_handler(&ab->input);
        istream_deinit_abort(&ab->output);
    }
}
