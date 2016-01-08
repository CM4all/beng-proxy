/*
 * OpenSSL global initialization.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_INIT_H
#define BENG_PROXY_SSL_INIT_H

void
ssl_global_init();

void
ssl_global_deinit();

/**
 * Free thread-local state.  Call this before exiting a thread.
 */
void
ssl_thread_deinit();

struct ScopeSslGlobalInit {
    ScopeSslGlobalInit() {
        ssl_global_init();
    }

    ~ScopeSslGlobalInit() {
        ssl_global_deinit();
    }

    ScopeSslGlobalInit(const ScopeSslGlobalInit &) = delete;
    ScopeSslGlobalInit &operator=(const ScopeSslGlobalInit &) = delete;
};

#endif
