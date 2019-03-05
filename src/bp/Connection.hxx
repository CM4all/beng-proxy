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
#include "pool/Ptr.hxx"

#include <boost/intrusive/list.hpp>

#include <chrono>

#include <stdint.h>

struct BpConfig;
struct BpInstance;
class UniqueSocketDescriptor;
class SocketAddress;
struct SslFactory;
struct HttpServerConnection;

/*
 * A connection from a HTTP client.
 */
struct BpConnection final
    : HttpServerConnectionHandler,
      boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    BpInstance &instance;
    PoolPtr pool;
    const BpConfig &config;

    const char *const listener_tag;

    const bool auth_alt_host;

    /**
     * The address (host and port) of the client.
     */
    const char *const remote_host_and_port;

    const LLogger logger;

    HttpServerConnection *http;

    /**
     * Attributes which are specific to the current request.  They are
     * only valid while a request is being handled (i.e. during the
     * lifetime of the #HttpServerRequest instance).  Strings are
     * allocated from the request pool.
     */
    struct PerRequest {
        /**
         * The time stamp at the start of the request.  Used to calculate
         * the request duration.
         */
        std::chrono::steady_clock::time_point start_time;

        /**
         * The name of the site being accessed by the current HTTP
         * request (from #TRANSLATE_SITE).  It is a hack to allow the
         * "log" callback to see this information.
         */
        const char *site_name;

        void Begin(std::chrono::steady_clock::time_point now) noexcept;

        std::chrono::steady_clock::duration GetDuration(std::chrono::steady_clock::time_point now) const noexcept {
            return now - start_time;
        }
    } per_request;

    BpConnection(PoolPtr &&_pool, BpInstance &_instance,
                 const char *_listener_tag, bool _auth_alt_host,
                 SocketAddress remote_address) noexcept;
    ~BpConnection() noexcept;

    struct Disposer {
        void operator()(BpConnection *c) noexcept;
    };

    /* virtual methods from class HttpServerConnectionHandler */
    void RequestHeadersFinished(const HttpServerRequest &request) noexcept override;
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) noexcept override;

    void LogHttpRequest(HttpServerRequest &request,
                        http_status_t status, off_t length,
                        uint64_t bytes_received,
                        uint64_t bytes_sent) noexcept override;

    void HttpConnectionError(std::exception_ptr e) noexcept override;
    void HttpConnectionClosed() noexcept override;
};

void
new_connection(BpInstance &instance,
               UniqueSocketDescriptor &&fd, SocketAddress address,
               SslFactory *ssl_factory,
               const char *listener_tag, bool auth_alt_host) noexcept;

void
close_connection(BpConnection *connection) noexcept;

#endif
