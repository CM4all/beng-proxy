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

#ifndef BENG_PROXY_CONNECTION_HXX
#define BENG_PROXY_CONNECTION_HXX

#include "http_server/Handler.hxx"
#include "io/Logger.hxx"

#include <boost/intrusive/list.hpp>

#include <chrono>

#include <stdint.h>

struct BpConfig;
struct BpInstance;
class UniqueSocketDescriptor;
class SocketAddress;
struct HttpServerConnection;

/*
 * A connection from a HTTP client.
 */
struct BpConnection final
    : HttpServerConnectionHandler,
      boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    BpInstance &instance;
    struct pool &pool;
    const BpConfig &config;

    const char *const listener_tag;

    /**
     * The address (host and port) of the client.
     */
    const char *const remote_host_and_port;

    const LLogger logger;

    HttpServerConnection *http;

    /**
     * The name of the site being accessed by the current HTTP
     * request.  This points to memory allocated by the request pool;
     * it is a hack to allow the "log" callback to see this
     * information.
     */
    const char *site_name = nullptr;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    std::chrono::steady_clock::time_point request_start_time;

    BpConnection(BpInstance &_instance, struct pool &_pool,
                 const char *_listener_tag,
                 SocketAddress remote_address);
    ~BpConnection();

    struct Disposer {
        void operator()(BpConnection *c);
    };

    /* virtual methods from class HttpServerConnectionHandler */
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) override;

    void LogHttpRequest(HttpServerRequest &request,
                        http_status_t status, off_t length,
                        uint64_t bytes_received, uint64_t bytes_sent) override;

    void HttpConnectionError(std::exception_ptr e) override;
    void HttpConnectionClosed() override;
};

void
new_connection(BpInstance &instance,
               UniqueSocketDescriptor &&fd, SocketAddress address,
               const char *listener_tag);

void
close_connection(BpConnection *connection);

#endif
