/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "strmap.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "handler.hxx"
#include "access_log.hxx"
#include "drop.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

BpConnection::BpConnection(BpInstance &_instance, struct pool &_pool,
                           const char *_listener_tag)
    :instance(_instance),
     pool(_pool),
     config(_instance.config),
     listener_tag(_listener_tag)
{
}

BpConnection::~BpConnection()
{
    if (http != nullptr)
        http_server_connection_close(http);
}

void
BpConnection::Disposer::operator()(BpConnection *c)
{
    auto &p = c->pool;
    DeleteFromPool(p, c);
    pool_trash(&p);
    pool_unref(&p);
}

void
close_connection(BpConnection *connection)
{
    auto &connections = connection->instance.connections;
    assert(!connections.empty());
    connections.erase_and_dispose(connections.iterator_to(*connection),
                                  BpConnection::Disposer());
}

gcc_pure
static int
HttpServerLogLevel(std::exception_ptr e)
{
    try {
        FindRetrowNested<std::system_error>(e);
    } catch (const std::system_error &se) {
        if (se.code().category() == ErrnoCategory() &&
            se.code().value() == ECONNRESET)
            return 4;
    }

    return 2;
}


/*
 * http connection handler
 *
 */

void
BpConnection::HandleHttpRequest(HttpServerRequest &request,
                                CancellablePointer &cancel_ptr)
{
    ++instance.http_request_counter;

    site_name = nullptr;
    request_start_time = std::chrono::steady_clock::now();

    handle_http_request(*this, request, cancel_ptr);
}

void
BpConnection::LogHttpRequest(HttpServerRequest &request,
                             http_status_t status, int64_t length,
                             uint64_t bytes_received, uint64_t bytes_sent)
{
    access_log(&request, site_name,
               request.headers.Get("referer"),
               request.headers.Get("user-agent"),
               status, length,
               bytes_received, bytes_sent,
               std::chrono::steady_clock::now() - request_start_time);
    site_name = nullptr;
}

void
BpConnection::HttpConnectionError(std::exception_ptr e)
{
    http = nullptr;

    daemon_log(HttpServerLogLevel(e), "%s\n", GetFullMessage(e).c_str());

    close_connection(this);
}

void
BpConnection::HttpConnectionClosed()
{
    http = nullptr;

    close_connection(this);
}

/*
 * listener handler
 *
 */

void
new_connection(BpInstance &instance,
               UniqueSocketDescriptor &&fd, SocketAddress address,
               const char *listener_tag)
{
    struct pool *pool;

    if (instance.connections.size() >= instance.config.max_connections) {
        unsigned num_dropped = drop_some_connections(&instance);

        if (num_dropped == 0) {
            daemon_log(1, "too many connections (%zu), dropping\n",
                       instance.connections.size());
            return;
        }
    }

    /* determine the local socket address */
    const StaticSocketAddress local_address = fd.GetLocalAddress();

    pool = pool_new_linear(instance.root_pool, "connection", 2048);
    pool_set_major(pool);

    auto *connection = NewFromPool<BpConnection>(*pool, instance, *pool,
                                                 listener_tag);
    instance.connections.push_front(*connection);

    connection->http =
        http_server_connection_new(pool,
                                   instance.event_loop,
                                   fd.Release(), FdType::FD_TCP,
                                   nullptr, nullptr,
                                   local_address.IsDefined()
                                   ? (SocketAddress)local_address
                                   : nullptr,
                                   address,
                                   true,
                                   *connection);
}
