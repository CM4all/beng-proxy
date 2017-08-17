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

#include "UdpDistribute.hxx"
#include "system/Error.hxx"
#include "util/DeleteDisposer.hxx"

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

UdpDistribute::Recipient::Recipient(EventLoop &_event_loop,
                                    UniqueSocketDescriptor &&_fd)
    :fd(std::move(_fd)),
     event(_event_loop, fd.Get(), SocketEvent::READ,
           BIND_THIS_METHOD(EventCallback))
{
    event.Add();
}

UdpDistribute::Recipient::~Recipient()
{
    event.Delete();
}

void
UdpDistribute::Clear()
{
    recipients.clear_and_dispose(DeleteDisposer());
}

UniqueSocketDescriptor
UdpDistribute::Add()
{
    UniqueSocketDescriptor result_fd, recipient_fd;
    if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_DGRAM, 0,
                                                  result_fd, recipient_fd))
        throw MakeErrno("socketpair() failed");

    auto *ur = new Recipient(event_loop, std::move(recipient_fd));
    recipients.push_back(*ur);
    return result_fd;
}

void
UdpDistribute::Packet(const void *payload, size_t payload_length)
{
    for (auto &ur : recipients)
        send(ur.fd.Get(), payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}
