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

#include "certdb/Config.hxx"
#include "ssl/NameCache.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "util/PrintException.hxx"

class Instance final : CertNameCacheHandler {
    EventLoop event_loop;
    ShutdownListener shutdown_listener;
    CertNameCache cache;

public:
    explicit Instance(const CertDatabaseConfig &config)
        :shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
         cache(event_loop, config, *this) {
        shutdown_listener.Enable();
        cache.Connect();
    }

    void Dispatch() noexcept {
        event_loop.Dispatch();
    }

private:
    void OnShutdown() noexcept {
        cache.Disconnect();
    }

    /* virtual methods from CertNameCacheHandler */
    void OnCertModified(const std::string &name,
                        bool deleted) noexcept override {
        fprintf(stderr, "%s: %s\n",
                deleted ? "deleted" : "modified",
                name.c_str());
    }
};

int
main(int argc, char **argv)
try {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s CONNINFO\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    CertDatabaseConfig config;
    config.connect = argv[1];

    Instance instance(config);
    instance.Dispatch();

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
