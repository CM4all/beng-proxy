/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Request.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

HttpServerRequest::HttpServerRequest(struct pool &_pool,
                                     HttpServerConnection &_connection,
                                     SocketAddress _local_address,
                                     SocketAddress _remote_address,
                                     const char *_local_host_and_port,
                                     const char *_remote_host_and_port,
                                     const char *_remote_host,
                                     http_method_t _method,
                                     StringView _uri)
    :pool(&_pool), connection(&_connection),
     local_address(_local_address),
     remote_address(_remote_address),
     local_host_and_port(_local_host_and_port),
     remote_host_and_port(_remote_host_and_port),
     remote_host(_remote_host),
     method(_method),
     uri(p_strdup(*pool, _uri)),
     headers(*pool) {}
