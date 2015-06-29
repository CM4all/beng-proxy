/*
 * This istream filter wraps data inside AJPv13 packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_ajp_body.hxx"
#include "istream/ForwardIstream.hxx"
#include "ajp_protocol.hxx"
#include "direct.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>

class AjpBodyIstream final : public ForwardIstream {
    struct istream output;
    struct istream *input;

    size_t requested = 0, packet_remaining = 0;

    gcc_packed struct {
        struct ajp_header header;
        uint16_t length;
    } header;
    size_t header_sent;

public:
    AjpBodyIstream(struct pool &pool, struct istream &_input)
        :ForwardIstream(pool, _input,
                        MakeIstreamHandler<AjpBodyIstream>::handler, this) {}

    static constexpr AjpBodyIstream &Cast2(struct istream &i) {
        return (AjpBodyIstream &)Istream::Cast(i);
    }

    void Request(size_t length) {
        /* we're not checking if this becomes larger than the request
           body - although Tomcat should know better, it requests more
           and more */
        requested += length;
    }

    void StartPacket(size_t length);

    /**
     * Returns true if the header is complete.
     */
    bool WriteHeader();

    /**
     * Returns true if the caller may write the packet body.
     */
    bool MakePacket(size_t length);

    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        return partial
            ? ForwardIstream::GetAvailable(partial)
            : -1;
    }

    off_t Skip(gcc_unused off_t length) override {
        return -1;
    }

    void Read() override;

    int AsFd() override {
        return -1;
    }

    /* handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(FdType type, int fd, size_t max_length);
};

void
AjpBodyIstream::StartPacket(size_t length)
{
    assert(requested > 0);
    assert(length > 0);

    if (length > requested)
        length = requested;

    if (length > 8192 - sizeof(header))
        /* limit packets to 8 kB - up to 65535 might be possible,
           but has never been tested */
        length = 8192 - sizeof(header);

    packet_remaining = length;
    requested -= length;

    header.header.a = 0x12;
    header.header.b = 0x34;
    header.header.length = ToBE16(packet_remaining + sizeof(header.length));
    header.length = ToBE16(packet_remaining);
    header_sent = 0;
}

bool
AjpBodyIstream::WriteHeader()
{
    assert(packet_remaining > 0);
    assert(header_sent <= sizeof(header));

    size_t length = sizeof(header) - header_sent;
    if (length == 0)
        return true;

    const char *p = (const char *)&header;
    p += header_sent;

    size_t nbytes = ForwardIstream::OnData(p, length);
    if (nbytes > 0)
        header_sent += nbytes;

    return nbytes == length;
}

bool
AjpBodyIstream::MakePacket(size_t length)
{
    if (packet_remaining == 0) {
        if (requested == 0)
            return false;

        StartPacket(length);
    }

    return WriteHeader();
}

/*
 * istream handler
 *
 */

size_t
AjpBodyIstream::OnData(const void *data, size_t length)
{
    if (!MakePacket(length))
        return 0;

    if (length > packet_remaining)
        length = packet_remaining;

    size_t nbytes = ForwardIstream::OnData(data, length);
    if (nbytes > 0)
        packet_remaining -= nbytes;

    return nbytes;
}

ssize_t
AjpBodyIstream::OnDirect(FdType type, int fd, size_t max_length)
{
    if (packet_remaining == 0) {
        if (requested == 0)
            return ISTREAM_RESULT_BLOCKING;

        /* start a new packet, size determined by
           direct_available() */
        ssize_t available = direct_available(fd, type, max_length);
        if (available <= 0)
            return available;

        StartPacket(available);
    }

    pool_ref(&GetPool());

    if (!WriteHeader()) {
        ssize_t ret = input != nullptr
            ? ISTREAM_RESULT_BLOCKING : ISTREAM_RESULT_CLOSED;
        pool_unref(&GetPool());
        return ret;
    }

    pool_unref(&GetPool());

    if (max_length > packet_remaining)
        max_length = packet_remaining;

    ssize_t nbytes = istream_invoke_direct(&output, type, fd, max_length);
    if (nbytes > 0)
        packet_remaining -= nbytes;

    return nbytes;
}

/*
 * istream implementation
 *
 */

void
AjpBodyIstream::Read()
{
    if (packet_remaining > 0 && !WriteHeader())
        return;

    if (packet_remaining == 0 && requested > 0) {
        /* start a new packet, as large as possible */
        off_t available = ForwardIstream::GetAvailable(true);
        if (available > 0)
            StartPacket(available);
    }

    ForwardIstream::Read();
}

/*
 * constructor
 *
 */

struct istream *
istream_ajp_body_new(struct pool *pool, struct istream *input)
{
    assert(input != NULL);
    assert(!istream_has_handler(input));

    return NewIstream<AjpBodyIstream>(*pool, *input);
}

void
istream_ajp_body_request(struct istream *istream, size_t length)
{
    auto &ab = AjpBodyIstream::Cast2(*istream);
    ab.Request(length);
}
