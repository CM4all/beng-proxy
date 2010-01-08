/*
 * Convert a stream into a stream of FCGI_STDIN packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "fcgi-protocol.h"

#include <assert.h>
#include <string.h>
#include <netinet/in.h>

struct istream_fcgi {
    struct istream output;
    istream_t input;

    size_t missing_from_current_record;

    struct fcgi_record_header header;
    size_t header_sent;
};

static bool
fcgi_write_header(struct istream_fcgi *fcgi)
{
    const char *header;
    size_t length, nbytes;

    assert(fcgi->header_sent <= sizeof(fcgi->header));

    length = sizeof(fcgi->header) - fcgi->header_sent;
    if (length == 0)
        return true;

    header = (char*)&fcgi->header + fcgi->header_sent;
    nbytes = istream_invoke_data(&fcgi->output, header, length);
    if (nbytes > 0)
        fcgi->header_sent += nbytes;

    return nbytes == length;
}

static void
fcgi_start_record(struct istream_fcgi *fcgi, size_t length)
{
    assert(fcgi->missing_from_current_record == 0);
    assert(fcgi->header_sent == sizeof(fcgi->header));

    if (length > 0xffff)
        /* uint16_t's limit */
        length = 0xffff;

    fcgi->header.content_length = htons(length);
    fcgi->header_sent = 0;
    fcgi->missing_from_current_record = length;
}

static size_t
fcgi_feed(struct istream_fcgi *fcgi, const char *data, size_t length)
{
    size_t total = 0, rest, nbytes;
    bool bret;

    assert(fcgi->input != NULL);

    while (true) {
        bret = fcgi_write_header(fcgi);
        if (!bret)
            return fcgi->input == NULL ? 0 : total;

        if (fcgi->missing_from_current_record > 0) {
            /* send the record header */
            rest = length - total;
            if (rest > fcgi->missing_from_current_record)
                rest = fcgi->missing_from_current_record;

            nbytes = istream_invoke_data(&fcgi->output, data + total, rest);
            if (nbytes == 0)
                return fcgi->input == NULL ? 0 : total;

            total += nbytes;
            fcgi->missing_from_current_record -= nbytes;

            if (fcgi->missing_from_current_record > 0)
                /* not enough data or handler is blocking - return for
                   now */
                return total;
        }

        rest = length - total;
        if (rest == 0)
            return total;

        fcgi_start_record(fcgi, rest);
    };

    return total;
}


/*
 * istream handler
 *
 */

static size_t
fcgi_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_fcgi *fcgi = ctx;
    size_t nbytes;

    pool_ref(fcgi->output.pool);
    nbytes = fcgi_feed(fcgi, (const char*)data, length);
    pool_unref(fcgi->output.pool);

    return nbytes;
}

static void
fcgi_input_eof(void *ctx)
{
    struct istream_fcgi *fcgi = ctx;
    bool bret;

    assert(fcgi->input != NULL);
    assert(fcgi->missing_from_current_record == 0);
    assert(fcgi->header_sent == sizeof(fcgi->header));

    fcgi->input = NULL;

    /* write EOF record (length 0) */

    fcgi_start_record(fcgi, 0);

    /* flush the buffer */

    bret = fcgi_write_header(fcgi);
    if (bret)
        istream_deinit_eof(&fcgi->output);
}

static const struct istream_handler fcgi_input_handler = {
    .data = fcgi_input_data,
    .eof = fcgi_input_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_fcgi *
istream_to_fcgi(istream_t istream)
{
    return (struct istream_fcgi *)(((char*)istream) - offsetof(struct istream_fcgi, output));
}

static void
istream_fcgi_read(istream_t istream)
{
    struct istream_fcgi *fcgi = istream_to_fcgi(istream);
    bool bret;

    bret = fcgi_write_header(fcgi);
    if (!bret)
        return;

    if (fcgi->input == NULL) {
        istream_deinit_eof(&fcgi->output);
        return;
    }

    if (fcgi->missing_from_current_record == 0) {
        off_t available = istream_available(fcgi->input, true);
        if (available > 0) {
            fcgi_start_record(fcgi, available);
            bret = fcgi_write_header(fcgi);
            if (!bret)
                return;
        }
    }

    istream_read(fcgi->input);
}

static void
istream_fcgi_close(istream_t istream)
{
    struct istream_fcgi *fcgi = istream_to_fcgi(istream);

    if (fcgi->input != NULL)
        istream_close_handler(fcgi->input);

    istream_deinit_abort(&fcgi->output);
}

static const struct istream istream_fcgi = {
    .read = istream_fcgi_read,
    .close = istream_fcgi_close,
};


/*
 * constructor
 *
 */

istream_t
istream_fcgi_new(pool_t pool, istream_t input, uint16_t request_id)
{
    struct istream_fcgi *fcgi = istream_new_macro(pool, fcgi);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    fcgi->missing_from_current_record = 0;
    fcgi->header_sent = sizeof(fcgi->header);
    fcgi->header = (struct fcgi_record_header){
        .version = FCGI_VERSION_1,
        .type = FCGI_STDIN,
        .request_id = request_id,
        .padding_length = 0,
        .reserved = 0,
    };

    istream_assign_handler(&fcgi->input, input,
                           &fcgi_input_handler, fcgi,
                           0);

    return istream_struct_cast(&fcgi->output);
}
