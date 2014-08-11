/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_init.hxx"

#include <inline/compiler.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>

#include <mutex>

#include <pthread.h>

static std::mutex *ssl_mutexes;

static void
locking_function(int mode, int n,
                 gcc_unused const char *file, gcc_unused int line)
{
    if (mode & CRYPTO_LOCK)
        ssl_mutexes[n].lock();
    else
        ssl_mutexes[n].unlock();
}

static unsigned long
id_function(void)
{
    return pthread_self();
}

void
ssl_global_init(void)
{
    SSL_load_error_strings();
    SSL_library_init();

    /* initialise OpenSSL multi-threading; this is needed because the
       SSL_CTX object is shared among all threads, which need to
       modify it in a safe manner */

    ssl_mutexes = new std::mutex[CRYPTO_num_locks()];

    CRYPTO_set_locking_callback(locking_function);
    CRYPTO_set_id_callback(id_function);
}

void
ssl_global_deinit(void)
{
    CRYPTO_set_id_callback(NULL);
    CRYPTO_set_locking_callback(NULL);

    delete[] ssl_mutexes;
}
