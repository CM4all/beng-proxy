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

#include "was/Client.hxx"
#include "was/Launch.hxx"
#include "was/Lease.hxx"
#include "stopwatch.hxx"
#include "lease.hxx"
#include "HttpResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/sink_fd.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/FileIstream.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "spawn/Config.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "io/Logger.hxx"
#include "io/SpliceSupport.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "util/StaticArray.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

struct Context final
    : PInstance, WasLease, HttpResponseHandler {

    WasProcess process;

    SinkFd *body = nullptr;
    bool error;

    CancellablePointer cancel_ptr;

    Context():body(nullptr) {}

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
                        UnusedIstreamPtr body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;
};

/*
 * SinkFdHandler
 *
 */

static void
my_sink_fd_input_eof(void *ctx)
{
    auto &c = *(Context *)ctx;

    c.body = nullptr;
}

static void
my_sink_fd_input_error(std::exception_ptr ep, void *ctx)
{
    auto &c = *(Context *)ctx;

    PrintException(ep);

    c.body = nullptr;
    c.error = true;
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    auto &c = *(Context *)ctx;

    fprintf(stderr, "%s\n", strerror(error));

    c.body = nullptr;
    c.error = true;

    return true;
}

static constexpr SinkFdHandler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};


/*
 * http_response_handler
 *
 */

void
Context::OnHttpResponse(http_status_t status,
                        gcc_unused StringMap &&headers,
                        UnusedIstreamPtr _body) noexcept
{
    fprintf(stderr, "status: %s\n", http_status_to_string(status));

    if (_body) {
        struct pool &pool = root_pool;
        body = sink_fd_new(event_loop, pool,
                           std::move(_body),
                           FileDescriptor(STDOUT_FILENO),
                           guess_fd_type(STDOUT_FILENO),
                           my_sink_fd_handler, this);
        sink_fd_read(body);
    }
}

void
Context::OnHttpError(std::exception_ptr ep) noexcept
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
                              "/dev/stdin", UniqueFileDescriptor(STDIN_FILENO),
                              FdType::FD_FILE, -1)
        : nullptr;
}

int
main(int argc, char **argv)
try {
    SetLogLevel(5);

    StaticArray<const char *, 64> params;

    if (argc < 3) {
        fprintf(stderr, "Usage: run_was PATH URI [--parameter a=b ...]\n");
        return EXIT_FAILURE;
    }

    const char *uri = argv[2];

    for (int i = 3; i < argc;) {
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
                                 child_options, {}, nullptr);

    was_client_request(context.root_pool, context.event_loop, nullptr,
                       context.process.control,
                       context.process.input,
                       context.process.output,
                       context,
                       HTTP_METHOD_GET, uri,
                       nullptr,
                       nullptr, nullptr,
                       *strmap_new(context.root_pool),
                       UnusedIstreamPtr(request_body(context.event_loop,
                                                     context.root_pool)),
                       { (const char *const*)params.raw(), params.size() },
                       context, context.cancel_ptr);

    context.event_loop.Dispatch();

    return context.error;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
