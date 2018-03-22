/*
 * Copyright 2007-2018 Content Management AG
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

#pragma once

#include <memory>
#include <string>

struct PreparedChildProcess;
class EventLoop;
class SocketDescriptor;
class UniqueFileDescriptor;
namespace Net::Log { struct Datagram; class PipeAdapter; }

/**
 * A glue class which manages where a child process logs its "stderr".
 */
class ChildErrorLog {
    std::unique_ptr<Net::Log::PipeAdapter> adapter;

    std::string site, uri;

public:
    ChildErrorLog() noexcept;

    /**
     * Construct a #Net::Log::PipeAdapter if the given socket is
     * defined.
     *
     * Throws on error.
     */
    ChildErrorLog(PreparedChildProcess &p,
                  EventLoop &event_loop, SocketDescriptor socket);

    ~ChildErrorLog() noexcept;

    ChildErrorLog(ChildErrorLog &&) = default;
    ChildErrorLog &operator=(ChildErrorLog &&);

    operator bool() const {
        return adapter != nullptr;
    }

    /**
     * @see Net::Log::PipeAdapter::GetDatagram()
     */
    Net::Log::Datagram &GetDatagram() noexcept;

    void SetSite(const char *_site) noexcept;
    void SetUri(const char *_uri) noexcept;

    /**
     * Throws on error.
     */
    UniqueFileDescriptor EnableClient(EventLoop &event_loop,
                                      SocketDescriptor socket);

    /**
     * Throws on error.
     */
    void EnableClient(PreparedChildProcess &p,
                      EventLoop &event_loop, SocketDescriptor socket);
};
