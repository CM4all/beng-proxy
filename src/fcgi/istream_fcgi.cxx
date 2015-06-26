/*
 * Convert a stream into a stream of FCGI_STDIN packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_fcgi.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "fcgi_protocol.h"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>
#include <string.h>

struct FcgiIstream {
    struct istream output;
    struct istream *input;

    size_t missing_from_current_record;

    struct fcgi_record_header header;
    size_t header_sent;

    bool WriteHeader();
    void StartRecord(size_t length);
    size_t Feed(const char *data, size_t length);
};

bool
FcgiIstream::WriteHeader()
{
    assert(header_sent <= sizeof(header));

    size_t length = sizeof(header) - header_sent;
    if (length == 0)
        return true;

    const char *data = (char *)&header + header_sent;
    size_t nbytes = istream_invoke_data(&output, data, length);
    if (nbytes > 0)
        header_sent += nbytes;

    return nbytes == length;
}

void
FcgiIstream::StartRecord(size_t length)
{
    assert(missing_from_current_record == 0);
    assert(header_sent == sizeof(header));

    if (length > 0xffff)
        /* uint16_t's limit */
        length = 0xffff;

    header.content_length = ToBE16(length);
    header_sent = 0;
    missing_from_current_record = length;
}

size_t
FcgiIstream::Feed(const char *data, size_t length)
{
    assert(input != nullptr);

    size_t total = 0;
    while (true) {
        if (!WriteHeader())
            return input == nullptr ? 0 : total;

        if (missing_from_current_record > 0) {
            /* send the record header */
            size_t rest = length - total;
            if (rest > missing_from_current_record)
                rest = missing_from_current_record;

            size_t nbytes = istream_invoke_data(&output,
                                                data + total, rest);
            if (nbytes == 0)
                return input == nullptr ? 0 : total;

            total += nbytes;
            missing_from_current_record -= nbytes;

            if (missing_from_current_record > 0)
                /* not enough data or handler is blocking - return for
                   now */
                return total;
        }

        size_t rest = length - total;
        if (rest == 0)
            return total;

        StartRecord(rest);
    }
}


/*
 * istream handler
 *
 */

static size_t
fcgi_input_data(const void *data, size_t length, void *ctx)
{
    auto *fcgi = (FcgiIstream *)ctx;

    const ScopePoolRef ref(*fcgi->output.pool TRACE_ARGS);
    return fcgi->Feed((const char *)data, length);
}

static void
fcgi_input_eof(void *ctx)
{
    auto *fcgi = (FcgiIstream *)ctx;

    assert(fcgi->input != nullptr);
    assert(fcgi->missing_from_current_record == 0);
    assert(fcgi->header_sent == sizeof(fcgi->header));

    fcgi->input = nullptr;

    /* write EOF record (length 0) */

    fcgi->StartRecord(0);

    /* flush the buffer */

    bool bret = fcgi->WriteHeader();
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

static inline FcgiIstream *
istream_to_fcgi(struct istream *istream)
{
    return &ContainerCast2(*istream, &FcgiIstream::output);
}

static void
istream_fcgi_read(struct istream *istream)
{
    FcgiIstream *fcgi = istream_to_fcgi(istream);

    bool bret = fcgi->WriteHeader();
    if (!bret)
        return;

    if (fcgi->input == nullptr) {
        istream_deinit_eof(&fcgi->output);
        return;
    }

    if (fcgi->missing_from_current_record == 0) {
        off_t available = istream_available(fcgi->input, true);
        if (available > 0) {
            fcgi->StartRecord(available);
            bret = fcgi->WriteHeader();
            if (!bret)
                return;
        }
    }

    istream_read(fcgi->input);
}

static void
istream_fcgi_close(struct istream *istream)
{
    FcgiIstream *fcgi = istream_to_fcgi(istream);

    if (fcgi->input != nullptr)
        istream_close_handler(fcgi->input);

    istream_deinit(&fcgi->output);
}

static const struct istream_class istream_fcgi = {
    .read = istream_fcgi_read,
    .close = istream_fcgi_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_fcgi_new(struct pool *pool, struct istream *input, uint16_t request_id)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto fcgi = NewFromPool<FcgiIstream>(*pool);
    istream_init(&fcgi->output, &istream_fcgi, pool);

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

    return &fcgi->output;
}
