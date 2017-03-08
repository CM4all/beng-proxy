#define HAVE_CHUNKED_REQUEST_BODY
#define ENABLE_HUGE_BODY
#define NO_EARLY_RELEASE_SOCKET // TODO: improve the WAS client

#include "t_client.hxx"
#include "tio.hxx"
#include "was/was_client.hxx"
#include "was/was_server.hxx"
#include "was/Lease.hxx"
#include "http_response.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "RootPool.hxx"
#include "fb_pool.hxx"
#include "event/Loop.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"

#include <inline/compiler.h>

#include <functional>

static void
RunNull(WasServer &server, struct pool &pool,
        gcc_unused http_method_t method,
        gcc_unused const char *uri, gcc_unused StringMap &&headers,
        Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(server, HTTP_STATUS_NO_CONTENT,
                        StringMap(pool), nullptr);
}

static void
RunHello(WasServer &server, struct pool &pool,
         gcc_unused http_method_t method,
         gcc_unused const char *uri, gcc_unused StringMap &&headers,
         Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(server, HTTP_STATUS_OK, StringMap(pool),
                        istream_string_new(&pool, "hello"));
}

static void
RunHuge(WasServer &server, struct pool &pool,
         gcc_unused http_method_t method,
         gcc_unused const char *uri, gcc_unused StringMap &&headers,
         Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(server, HTTP_STATUS_OK, StringMap(pool),
                        istream_head_new(&pool,
                                         *istream_zero_new(&pool),
                                         524288, true));
}

static void
RunHold(WasServer &server, struct pool &pool,
        gcc_unused http_method_t method,
        gcc_unused const char *uri, gcc_unused StringMap &&headers,
        Istream *body)
{
    if (body != nullptr)
        body->Close();

    was_server_response(server, HTTP_STATUS_OK, StringMap(pool),
                        istream_block_new(pool));
}

static void
RunMirror(WasServer &server, gcc_unused struct pool &pool,
          gcc_unused http_method_t method,
          gcc_unused const char *uri, StringMap &&headers,
          Istream *body)
{
    was_server_response(server,
                        body != nullptr ? HTTP_STATUS_OK : HTTP_STATUS_NO_CONTENT,
                        std::move(headers), body);
}

class WasConnection final : WasServerHandler, WasLease {
    EventLoop &event_loop;

    FileDescriptor control_fd, input_fd, output_fd;

    WasServer *server;

    Lease *lease;

    typedef std::function<void(WasServer &server, struct pool &pool,
                               http_method_t method,
                               const char *uri, StringMap &&headers,
                               Istream *body)> Callback;

    const Callback callback;

public:
    WasConnection(struct pool &pool, EventLoop &_event_loop,
                  Callback &&_callback)
        :event_loop(_event_loop), callback(std::move(_callback)) {
        FileDescriptor input_w;
        if (!FileDescriptor::CreatePipeNonBlock(input_fd, input_w)) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        FileDescriptor output_r;
        if (!FileDescriptor::CreatePipeNonBlock(output_r, output_fd)) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        FileDescriptor control_server;
        if (!FileDescriptor::CreateSocketPairNonBlock(AF_LOCAL, SOCK_STREAM, 0,
                                                      control_fd,
                                                      control_server)) {
            perror("socketpair");
            exit(EXIT_FAILURE);
        }

        server = was_server_new(pool, event_loop, control_server.Get(),
                                output_r.Get(), input_w.Get(), *this);
    }

    ~WasConnection() {
        control_fd.Close();
        input_fd.Close();
        output_fd.Close();

        if (server != nullptr)
            was_server_free(server);
    }

    void Request(struct pool *pool,
                 Lease &_lease,
                 http_method_t method, const char *uri,
                 StringMap &&headers, Istream *body,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr) {
        lease = &_lease;
        was_client_request(*pool, event_loop, nullptr,
                           control_fd.Get(), input_fd.Get(), output_fd.Get(),
                           *this,
                           method, uri, uri, nullptr, nullptr,
                           headers, body, nullptr,
                           handler, cancel_ptr);
    }

    /* virtual methods from class WasServerHandler */

    void OnWasRequest(struct pool &pool, http_method_t method,
                      const char *uri, StringMap &&headers,
                      Istream *body) override {
        callback(*server, pool, method, uri, std::move(headers), body);
    }

    void OnWasClosed() override {
        server = nullptr;
    }

    /* constructors */

    static WasConnection *NewMirror(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunMirror);
    }

    static WasConnection *NewNull(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunNull);
    }

    static WasConnection *NewDummy(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunHello);
    }

    static WasConnection *NewFixed(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunHello);
    }

    static WasConnection *NewTiny(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunHello);
    }

    static WasConnection *NewHuge(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunHuge);
    }

    static WasConnection *NewHold(struct pool &pool, EventLoop &event_loop) {
        return new WasConnection(pool, event_loop, RunHold);
    }

private:
    /* virtual methods from class WasLease */
    void ReleaseWas(bool reuse) override {
        lease->ReleaseLease(reuse);
    }

    void ReleaseWasStop(gcc_unused uint64_t input_received) override {
        ReleaseWas(false);
    }
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SetupProcess();
    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    EventLoop event_loop;

    run_all_tests<WasConnection>(RootPool());
}
