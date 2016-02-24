#define HAVE_CHUNKED_REQUEST_BODY
#define ENABLE_HUGE_BODY
#define NO_EARLY_RELEASE_SOCKET // TODO: improve the WAS client

#include "t_client.hxx"
#include "tio.hxx"
#include "was/was_client.hxx"
#include "was/was_server.hxx"
#include "was/Lease.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "system/fd-util.h"
#include "system/fd_util.h"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "RootPool.hxx"
#include "fb_pool.hxx"
#include "event/Event.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"

#include <inline/compiler.h>

#include <functional>

static void
RunNull(WasServer &server, gcc_unused struct pool &pool,
        gcc_unused http_method_t method,
        gcc_unused const char *uri, gcc_unused struct strmap &&headers,
        Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(&server, HTTP_STATUS_NO_CONTENT, nullptr, nullptr);
}

static void
RunHello(WasServer &server, struct pool &pool,
         gcc_unused http_method_t method,
         gcc_unused const char *uri, gcc_unused struct strmap &&headers,
         Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(&server, HTTP_STATUS_OK, nullptr,
                        istream_string_new(&pool, "hello"));
}

static void
RunHuge(WasServer &server, struct pool &pool,
         gcc_unused http_method_t method,
         gcc_unused const char *uri, gcc_unused struct strmap &&headers,
         Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(&server, HTTP_STATUS_OK, nullptr,
                        istream_head_new(&pool,
                                         *istream_zero_new(&pool),
                                         524288, true));
}

static void
RunHold(WasServer &server, struct pool &pool,
        gcc_unused http_method_t method,
        gcc_unused const char *uri, gcc_unused struct strmap &&headers,
        Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(&server, HTTP_STATUS_OK, nullptr,
                        istream_block_new(pool));
}

static void
RunMirror(WasServer &server, gcc_unused struct pool &pool,
          gcc_unused http_method_t method,
          gcc_unused const char *uri, struct strmap &&headers,
          Istream *body)
{
    was_server_response(&server,
                        body != nullptr ? HTTP_STATUS_OK : HTTP_STATUS_NO_CONTENT,
                        &headers, body);
}

class WasConnection final : WasServerHandler, WasLease {
    int control_fd, input_fd, output_fd;

    WasServer *server;

    Lease *lease;

    typedef std::function<void(WasServer &server, struct pool &pool,
                               http_method_t method,
                               const char *uri, struct strmap &&headers,
                               Istream *body)> Callback;

    const Callback callback;

public:
    WasConnection(struct pool &pool, Callback &&_callback)
        :callback(std::move(_callback)) {
        int a[2];
        if (pipe_cloexec_nonblock(a) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        int b[2];
        if (pipe_cloexec_nonblock(b) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        int c[2];
        if (socketpair_cloexec_nonblock(AF_LOCAL, SOCK_STREAM, 0, c) < 0) {
            perror("socketpair");
            exit(EXIT_FAILURE);
        }

        control_fd = c[0];
        input_fd = a[0];
        output_fd = b[1];

        server = was_server_new(&pool, c[1], b[0], a[1], *this);
    }

    ~WasConnection() {
        close(control_fd);
        close(input_fd);
        close(output_fd);

        if (server != nullptr)
            was_server_free(server);
    }

    void Request(struct pool *pool,
                 Lease &_lease,
                 http_method_t method, const char *uri,
                 struct strmap *headers, Istream *body,
                 const struct http_response_handler *handler,
                 void *ctx,
                 struct async_operation_ref *async_ref) {
        lease = &_lease;
        was_client_request(pool, control_fd, input_fd, output_fd, *this,
                           method, uri, uri, nullptr, nullptr,
                           headers, body, nullptr,
                           handler, ctx, async_ref);
    }

    /* virtual methods from class WasServerHandler */

    void OnWasRequest(struct pool &pool, http_method_t method,
                      const char *uri, struct strmap &&headers,
                      Istream *body) override {
        callback(*server, pool, method, uri, std::move(headers), body);
    }

    void OnWasClosed() override {
        server = nullptr;
    }

    /* constructors */

    static WasConnection *NewMirror(struct pool &pool) {
        return new WasConnection(pool, RunMirror);
    }

    static WasConnection *NewNull(struct pool &pool) {
        return new WasConnection(pool, RunNull);
    }

    static WasConnection *NewDummy(struct pool &pool) {
        return new WasConnection(pool, RunHello);
    }

    static WasConnection *NewFixed(struct pool &pool) {
        return new WasConnection(pool, RunHello);
    }

    static WasConnection *NewTiny(struct pool &pool) {
        return new WasConnection(pool, RunHello);
    }

    static WasConnection *NewHuge(struct pool &pool) {
        return new WasConnection(pool, RunHuge);
    }

    static WasConnection *NewHold(struct pool &pool) {
        return new WasConnection(pool, RunHold);
    }

private:
    /* virtual methods from class WasLease */
    void ReleaseWas(bool reuse) override {
        lease->ReleaseLease(reuse);
    }
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    EventBase event_base;
    fb_pool_init(false);

    run_all_tests<WasConnection>(RootPool());

    fb_pool_deinit();
}
