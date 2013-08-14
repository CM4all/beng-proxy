/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_init.hxx"

#include <inline/compiler.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>

#include <pthread.h>

#ifndef NDEBUG
static unsigned num_locks;
#endif

static pthread_mutex_t *ssl_mutexes;

static void
locking_function(int mode, int n,
                 gcc_unused const char *file, gcc_unused int line)
{
    if (mode & CRYPTO_LOCK)
        pthread_mutex_lock(&ssl_mutexes[n]);
    else
        pthread_mutex_unlock(&ssl_mutexes[n]);
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

#ifdef NDEBUG
    unsigned num_locks;
#endif
    num_locks = CRYPTO_num_locks();
    ssl_mutexes = new pthread_mutex_t[num_locks];
    for (unsigned i = 0; i < num_locks; ++i)
        pthread_mutex_init(&ssl_mutexes[i], NULL);

    CRYPTO_set_locking_callback(locking_function);
    CRYPTO_set_id_callback(id_function);
}

void
ssl_global_deinit(void)
{
    CRYPTO_set_id_callback(NULL);
    CRYPTO_set_locking_callback(NULL);

#ifdef NDEBUG
    const unsigned num_locks = CRYPTO_num_locks();
#endif

    for (unsigned i = 0; i < num_locks; ++i)
        pthread_mutex_destroy(&ssl_mutexes[i]);
    delete[] ssl_mutexes;
}
