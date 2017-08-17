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

#ifndef UDP_LISTENER_HXX
#define UDP_LISTENER_HXX

#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <stddef.h>

class SocketAddress;
class UdpHandler;

/**
 * Listener on a UDP port.
 */
class UdpListener {
    UniqueSocketDescriptor fd;
    SocketEvent event;

    UdpHandler &handler;

public:
    UdpListener(EventLoop &event_loop, UniqueSocketDescriptor &&_fd,
                UdpHandler &_handler);
    ~UdpListener();

    /**
     * Enable the object after it has been disabled by Disable().  A
     * new object is enabled by default.
     */
    void Enable() {
        event.Add();
    }

    /**
     * Disable the object temporarily.  To undo this, call Enable().
     */
    void Disable() {
        event.Delete();
    }

    /**
     * Replaces the socket.  The old one is closed, and the new one is now
     * owned by this object.
     *
     * This may only be called on an object that is "enabled", see
     * Enable().
     */
    void SetFd(UniqueSocketDescriptor &&_fd);

    /**
     * Send a reply datagram to a client.
     *
     * Throws std::runtime_error on error.
     */
    void Reply(SocketAddress address,
               const void *data, size_t data_length);

private:
    void EventCallback(unsigned events);
};

#endif
