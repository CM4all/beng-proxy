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

#ifndef UDP_DISTRIBUTE_HXX
#define UDP_DISTRIBUTE_HXX

#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <boost/intrusive/list.hpp>

#include <stddef.h>

class EventLoop;

/**
 * Distribute UDP (control) packets to all workers.
 */
class UdpDistribute {
    struct Recipient
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {

        UniqueSocketDescriptor fd;
        SocketEvent event;

        Recipient(EventLoop &_event_loop, UniqueSocketDescriptor &&_fd);
        ~Recipient();

        void RemoveAndDestroy() {
            delete this;
        }

    private:
        void EventCallback(unsigned) {
            RemoveAndDestroy();
        }
    };

    EventLoop &event_loop;

    boost::intrusive::list<Recipient,
                           boost::intrusive::constant_time_size<false>> recipients;

public:
    explicit UdpDistribute(EventLoop &_event_loop):event_loop(_event_loop) {}

    ~UdpDistribute() {
        Clear();
    }

    /**
     * Throws std::system_error on error.
     */
    UniqueSocketDescriptor Add();
    void Clear();

    void Packet(const void *payload, size_t payload_length);
};

#endif
