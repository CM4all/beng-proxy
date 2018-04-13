/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "istream_ajp_body.hxx"
#include "Protocol.hxx"
#include "istream/ForwardIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "direct.hxx"
#include "util/ByteOrder.hxx"
#include "util/DestructObserver.hxx"

#include <assert.h>

class AjpBodyIstream final : public ForwardIstream, DestructAnchor {
    const SharedPoolPtr<AjpBodyIstreamControl> control;

    size_t requested = 0, packet_remaining = 0;

    gcc_packed struct {
        struct ajp_header header;
        uint16_t length;
    } header;
    size_t header_sent;

public:
    AjpBodyIstream(struct pool &_pool, UnusedIstreamPtr &&_input) noexcept
        :ForwardIstream(_pool, std::move(_input)),
         control(SharedPoolPtr<AjpBodyIstreamControl>::Make(_pool, *this)) {}

    ~AjpBodyIstream() noexcept {
        control->body = nullptr;
    }

    auto GetControl() noexcept {
        return control;
    }

    void Request(size_t length) noexcept {
        /* we're not checking if this becomes larger than the request
           body - although Tomcat should know better, it requests more
           and more */
        requested += length;
    }

private:
    void StartPacket(size_t length) noexcept;

    /**
     * Returns true if the header is complete.
     */
    bool WriteHeader() noexcept;

    /**
     * Returns true if the caller may write the packet body.
     */
    bool MakePacket(size_t length) noexcept;

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) noexcept override {
        return partial
            ? ForwardIstream::_GetAvailable(partial)
            : -1;
    }

    off_t _Skip(gcc_unused off_t length) noexcept override {
        return -1;
    }

    void _Read() noexcept override;

    int _AsFd() noexcept override {
        return -1;
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
};

void
AjpBodyIstream::StartPacket(size_t length) noexcept
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
AjpBodyIstream::WriteHeader() noexcept
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
AjpBodyIstream::MakePacket(size_t length) noexcept
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
AjpBodyIstream::OnData(const void *data, size_t length) noexcept
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
AjpBodyIstream::OnDirect(FdType type, int fd, size_t max_length) noexcept
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

    const DestructObserver destructed(*this);

    if (!WriteHeader()) {
        ssize_t ret = destructed
            ? ISTREAM_RESULT_CLOSED : ISTREAM_RESULT_BLOCKING;
        return ret;
    }

    if (max_length > packet_remaining)
        max_length = packet_remaining;

    ssize_t nbytes = InvokeDirect(type, fd, max_length);
    if (nbytes > 0)
        packet_remaining -= nbytes;

    return nbytes;
}

/*
 * istream implementation
 *
 */

void
AjpBodyIstream::_Read() noexcept
{
    if (packet_remaining > 0 && !WriteHeader())
        return;

    if (packet_remaining == 0 && requested > 0) {
        /* start a new packet, as large as possible */
        off_t available = ForwardIstream::_GetAvailable(true);
        if (available > 0)
            StartPacket(available);
    }

    ForwardIstream::_Read();
}

/*
 * constructor
 *
 */

std::pair<UnusedIstreamPtr, SharedPoolPtr<AjpBodyIstreamControl>>
istream_ajp_body_new(struct pool &pool, UnusedIstreamPtr input) noexcept
{
    auto *i = NewIstream<AjpBodyIstream>(pool, std::move(input));
    return std::make_pair(UnusedIstreamPtr(i), i->GetControl());
}

void
AjpBodyIstreamControl::Request(size_t length) noexcept
{
    if (body != nullptr)
        body->Request(length);
}
