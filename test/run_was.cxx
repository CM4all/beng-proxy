#include "was/Client.hxx"
#include "was/Launch.hxx"
#include "was/Lease.hxx"
#include "lease.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/Pointer.hxx"
#include "istream/istream_file.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "spawn/Config.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "util/StaticArray.hxx"

#include <daemon/log.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

struct Context final
    : PInstance, WasLease, IstreamHandler, HttpResponseHandler {

    WasProcess process;

    IstreamPointer body;
    bool error;

    CancellablePointer cancel_ptr;

    Context():body(nullptr) {}

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override;

    void OnEof() override {
        body.Clear();
    }

    void OnError(std::exception_ptr ep) override {
        PrintException(ep);

        body.Clear();
        error = true;
    }

    /* virtual methods from class Lease */
    void ReleaseWas(gcc_unused bool reuse) override {
        kill(process.pid, SIGTERM);

        process.Close();
    }

    void ReleaseWasStop(gcc_unused uint64_t input_received) override {
        ReleaseWas(false);
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(std::exception_ptr ep) override;
};

/*
 * istream handler
 *
 */

size_t
Context::OnData(const void *data, size_t length)
{
    ssize_t nbytes = write(1, data, length);
    if (nbytes <= 0) {
        error = true;
        body.ClearAndClose();
        return 0;
    }

    return (size_t)nbytes;
}

/*
 * http_response_handler
 *
 */

void
Context::OnHttpResponse(gcc_unused http_status_t status,
                        gcc_unused StringMap &&headers,
                        Istream *_body)
{
    if (_body != nullptr)
        body.Set(*_body, *this);
}

void
Context::OnHttpError(std::exception_ptr ep)
{
    PrintException(ep);

    error = true;
}

static Istream *
request_body(EventLoop &event_loop, struct pool &pool)
{
    struct stat st;
    return fstat(0, &st) == 0 && S_ISREG(st.st_mode)
        ? istream_file_fd_new(event_loop, pool,
                              "/dev/stdin", 0, FdType::FD_FILE, -1)
        : nullptr;
}

int
main(int argc, char **argv)
try {
    daemon_log_config.verbose = 5;

    StaticArray<const char *, 64> params;

    if (argc < 2) {
        fprintf(stderr, "Usage: run_was PATH [--parameter a=b ...]\n");
        return EXIT_FAILURE;
    }

    for (int i = 2; i < argc;) {
        if (strcmp(argv[i], "--parameter") == 0 ||
            strcmp(argv[i], "-p") == 0) {
            ++i;
            if (i >= argc)
                throw std::runtime_error("Parameter value missing");

            if (params.full())
                throw std::runtime_error("Too many parameters");

            params.push_back(argv[i++]);
        } else
            throw std::runtime_error("Unrecognized parameter");
    }

    direct_global_init();

    SpawnConfig spawn_config;

    const ScopeFbPoolInit fb_pool_init;

    ChildOptions child_options;

    Context context;
    ChildProcessRegistry child_process_registry(context.event_loop);
    child_process_registry.SetVolatile();
    LocalSpawnService spawn_service(spawn_config, child_process_registry);

    context.process = was_launch(spawn_service, "was",
                                 argv[1], nullptr,
                                 child_options, nullptr);

    was_client_request(context.root_pool, context.event_loop, nullptr,
                       context.process.control.Get(),
                       context.process.input.Get(),
                       context.process.output.Get(),
                       context,
                       HTTP_METHOD_GET, "/",
                       nullptr,
                       nullptr, nullptr,
                       *strmap_new(context.root_pool),
                       request_body(context.event_loop, context.root_pool),
                       { (const char *const*)params.raw(), params.size() },
                       context, context.cancel_ptr);

    context.event_loop.Dispatch();

    return context.error;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
