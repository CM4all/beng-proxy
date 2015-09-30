/*
 * Convert a stream into a stream of FCGI_STDIN packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_fcgi.hxx"
#include "Protocol.hxx"
#include "istream/FacadeIstream.hxx"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>
#include <string.h>

class FcgiIstream final : public FacadeIstream {
    size_t missing_from_current_record = 0;

    struct fcgi_record_header header;
    size_t header_sent = sizeof(header);

public:
    FcgiIstream(struct pool &pool, struct istream &_input,
                uint16_t request_id)
        :FacadeIstream(pool, _input,
                       MakeIstreamHandler<FcgiIstream>::handler, this) {
        header = (struct fcgi_record_header){
            .version = FCGI_VERSION_1,
            .type = FCGI_STDIN,
            .request_id = request_id,
            .content_length = 0,
            .padding_length = 0,
            .reserved = 0,
        };
    }

    bool WriteHeader();
    void StartRecord(size_t length);
    size_t Feed(const char *data, size_t length);

    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        return partial
            ? sizeof(header) - header_sent + input.GetAvailable(partial)
            : -1;
    }

    off_t Skip(gcc_unused off_t length) override {
        return -1;
    }

    void Read() override;

    int AsFd() override {
        return -1;
    }

    void Close() override;

    /* handler */

    size_t OnData(const void *data, size_t length) {
        const ScopePoolRef ref(GetPool() TRACE_ARGS);
        return Feed((const char *)data, length);
    }

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof();

    void OnError(GError *error) {
        ClearInput();
        DestroyError(error);
    }
};

bool
FcgiIstream::WriteHeader()
{
    assert(header_sent <= sizeof(header));

    size_t length = sizeof(header) - header_sent;
    if (length == 0)
        return true;

    const char *data = (char *)&header + header_sent;
    size_t nbytes = InvokeData(data, length);
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
    assert(HasInput());

    size_t total = 0;
    while (true) {
        if (!WriteHeader())
            return HasInput() ? total : 0;

        if (missing_from_current_record > 0) {
            /* send the record header */
            size_t rest = length - total;
            if (rest > missing_from_current_record)
                rest = missing_from_current_record;

            size_t nbytes = InvokeData(data + total, rest);
            if (nbytes == 0)
                return HasInput() ? total : 0;

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

void
FcgiIstream::OnEof()
{
    assert(HasInput());
    assert(missing_from_current_record == 0);
    assert(header_sent == sizeof(header));

    ClearInput();

    /* write EOF record (length 0) */

    StartRecord(0);

    /* flush the buffer */

    if (WriteHeader())
        DestroyEof();
}

/*
 * istream implementation
 *
 */

void
FcgiIstream::Read()
{
    if (!WriteHeader())
        return;

    if (!HasInput()) {
        DestroyEof();
        return;
    }

    if (missing_from_current_record == 0) {
        off_t available = input.GetAvailable(true);
        if (available > 0) {
            StartRecord(available);
            if (!WriteHeader())
                return;
        }
    }

    input.Read();
}

void
FcgiIstream::Close()
{
    if (HasInput())
        input.ClearAndClose();

    Destroy();
}

/*
 * constructor
 *
 */

struct istream *
istream_fcgi_new(struct pool *pool, struct istream *input, uint16_t request_id)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    return NewIstream<FcgiIstream>(*pool, *input, request_id);
}
