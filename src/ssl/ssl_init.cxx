/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_init.hxx"
#include "FifoBufferBio.hxx"

#include <inline/compiler.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/err.h>

#include <mutex>

#include <pthread.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L

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
id_function()
{
    return pthread_self();
}

#endif

void
ssl_global_init()
{
    SSL_load_error_strings();
    SSL_library_init();
    ENGINE_load_builtin_engines();

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    /* initialise OpenSSL multi-threading; this is needed because the
       SSL_CTX object is shared among all threads, which need to
       modify it in a safe manner */

    ssl_mutexes = new std::mutex[CRYPTO_num_locks()];

    CRYPTO_set_locking_callback(locking_function);
    CRYPTO_set_id_callback(id_function);
#endif
}

void
ssl_global_deinit()
{
    DeinitFifoBufferBio();

    ENGINE_cleanup();
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();

    CRYPTO_set_id_callback(nullptr);
    CRYPTO_set_locking_callback(nullptr);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    delete[] ssl_mutexes;
#endif

    ERR_free_strings();

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
    ERR_remove_thread_state(nullptr);
#else
    ERR_remove_state(0);
#endif
#endif
}

void
ssl_thread_deinit()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
    ERR_remove_thread_state(nullptr);
#else
    ERR_remove_state(0);
#endif
#endif
}
