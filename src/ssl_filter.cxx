/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_filter.hxx"
#include "ssl_factory.hxx"
#include "ssl_config.hxx"
#include "ssl_quark.hxx"
#include "thread_socket_filter.hxx"
#include "pool.hxx"
#include "gerrno.h"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <assert.h>
#include <string.h>

struct ssl_filter {
    /**
     * Buffers which can be accessed from within the thread without
     * holding locks.  These will be copied to/from the according
     * #thread_socket_filter buffers.
     */
    SliceFifoBuffer decrypted_input, plain_output;

    /**
     * Memory BIOs for passing data to/from OpenSSL.
     */
    BIO *encrypted_input, *encrypted_output;

    SSL *ssl;

    bool handshaking;

    char *peer_subject, *peer_issuer_subject;
};

static void
ssl_set_error(GError **error_r)
{
    if (error_r == NULL)
        return;

    unsigned long error = ERR_get_error();
    char buffer[120];
    g_set_error(error_r, ssl_quark(), 0, "%s",
                ERR_error_string(error, buffer));
}

/**
 * Move data from #src to #dest.
 */
static void
Move(BIO *dest, ForeignFifoBuffer<uint8_t> &src)
{
    auto r = src.Read();
    if (r.IsEmpty())
        return;

    int nbytes = BIO_write(dest, r.data, r.size);
    if (nbytes > 0)
        src.Consume(nbytes);
}

static void
Move(ForeignFifoBuffer<uint8_t> &dest, BIO *src)
{
    while (true) {
        auto w = dest.Write();
        if (w.IsEmpty())
            return;

        int nbytes = BIO_read(src, w.data, w.size);
        if (nbytes <= 0)
            return;

        dest.Append(nbytes);
    }
}

static char *
format_name(X509_NAME *name)
{
    if (name == NULL)
        return NULL;

    BIO *bio = BIO_new(BIO_s_mem());
    if (bio == NULL)
        return NULL;

    X509_NAME_print_ex(bio, name, 0,
                       ASN1_STRFLGS_UTF8_CONVERT | XN_FLAG_SEP_COMMA_PLUS);
    char buffer[1024];
    int length = BIO_read(bio, buffer, sizeof(buffer) - 1);
    BIO_free(bio);

    return strndup(buffer, length);
}

static char *
format_subject_name(X509 *cert)
{
    return format_name(X509_get_subject_name(cert));
}

static char *
format_issuer_subject_name(X509 *cert)
{
    return format_name(X509_get_issuer_name(cert));
}

gcc_pure
static bool
is_ssl_error(SSL *ssl, int ret)
{
    if (ret == 0)
        /* this is always an error according to the documentation of
           SSL_read(), SSL_write() and SSL_do_handshake() */
        return true;

    switch (SSL_get_error(ssl, ret)) {
    case SSL_ERROR_NONE:
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_CONNECT:
    case SSL_ERROR_WANT_ACCEPT:
        return false;

    default:
        return true;
    }
}

static bool
check_ssl_error(SSL *ssl, int result, GError **error_r)
{
    if (is_ssl_error(ssl, result)) {
        ssl_set_error(error_r);
        return false;
    } else
        return true;
}

/**
 * @return false on error
 */
static bool
ssl_decrypt(SSL *ssl, ForeignFifoBuffer<uint8_t> &buffer, GError **error_r)
{
    /* SSL_read() must be called repeatedly until there is no more
       data (or until the buffer is full) */

    while (true) {
        auto w = buffer.Write();
        if (w.IsEmpty())
            return 0;

        int result = SSL_read(ssl, w.data, w.size);
        if (result <= 0)
            return check_ssl_error(ssl, result, error_r);

        buffer.Append(result);
    }
}

/**
 * @return false on error
 */
static bool
ssl_encrypt(SSL *ssl, ForeignFifoBuffer<uint8_t> &buffer, GError **error_r)
{
    auto r = buffer.Read();
    if (r.IsEmpty())
        return true;

    int result = SSL_write(ssl, r.data, r.size);
    if (result <= 0)
        return check_ssl_error(ssl, result, error_r);

    buffer.Consume(result);
    return true;
}

/*
 * thread_socket_filter_handler
 *
 */

static bool
ssl_thread_socket_filter_run(ThreadSocketFilter &f, GError **error_r,
                             void *ctx)
{
    struct ssl_filter *ssl = (struct ssl_filter *)ctx;

    /* copy input (and output to make room for more output) */

    f.mutex.lock();
    f.decrypted_input.MoveFrom(ssl->decrypted_input);
    ssl->plain_output.MoveFrom(f.plain_output);
    Move(ssl->encrypted_input, f.encrypted_input);
    Move(f.encrypted_output, ssl->encrypted_output);
    f.mutex.unlock();

    /* let OpenSSL work */

    ERR_clear_error();

    if (gcc_unlikely(ssl->handshaking)) {
        int result = SSL_do_handshake(ssl->ssl);
        if (result == 1) {
            ssl->handshaking = false;

            X509 *cert = SSL_get_peer_certificate(ssl->ssl);
            if (cert != nullptr) {
                ssl->peer_subject = format_subject_name(cert);
                ssl->peer_issuer_subject = format_issuer_subject_name(cert);
                X509_free(cert);
            }
        } else if (!check_ssl_error(ssl->ssl, result, error_r))
            return false;
    }

    if (gcc_likely(!ssl->handshaking) &&
        (!ssl_encrypt(ssl->ssl, ssl->plain_output, error_r) ||
         !ssl_decrypt(ssl->ssl, ssl->decrypted_input, error_r)))
        return false;

    /* copy output */

    f.mutex.lock();
    f.decrypted_input.MoveFrom(ssl->decrypted_input);
    Move(f.encrypted_output, ssl->encrypted_output);
    f.drained = ssl->plain_output.IsEmpty() &&
        BIO_eof(ssl->encrypted_output);
    f.mutex.unlock();

    return true;
}

static void
ssl_thread_socket_filter_destroy(gcc_unused ThreadSocketFilter &f, void *ctx)
{
    struct ssl_filter *ssl = (struct ssl_filter *)ctx;

    if (ssl->ssl != NULL)
        SSL_free(ssl->ssl);

    ssl->decrypted_input.Free(fb_pool_get());
    ssl->plain_output.Free(fb_pool_get());

    free(ssl->peer_subject);
    free(ssl->peer_issuer_subject);
}

const struct ThreadSocketFilterHandler ssl_thread_socket_filter = {
    ssl_thread_socket_filter_run,
    ssl_thread_socket_filter_destroy,
};

/*
 * constructor
 *
 */

struct ssl_filter *
ssl_filter_new(struct pool *pool, ssl_factory &factory,
               GError **error_r)
{
    assert(pool != NULL);

    ssl_filter *ssl = NewFromPool<ssl_filter>(*pool);

    ssl->ssl = ssl_factory_make(factory);
    if (ssl->ssl == NULL) {
        g_set_error(error_r, ssl_quark(), 0, "SSL_new() failed");
        return NULL;
    }

    ssl->decrypted_input.Allocate(fb_pool_get());
    ssl->plain_output.Allocate(fb_pool_get());
    ssl->encrypted_input = BIO_new(BIO_s_mem());
    ssl->encrypted_output = BIO_new(BIO_s_mem());

    SSL_set_bio(ssl->ssl, ssl->encrypted_input, ssl->encrypted_output);

    ssl->peer_subject = NULL;
    ssl->peer_issuer_subject = NULL;
    ssl->handshaking = true;

    return ssl;
}

const char *
ssl_filter_get_peer_subject(struct ssl_filter *ssl)
{
    assert(ssl != NULL);

    return ssl->peer_subject;
}

const char *
ssl_filter_get_peer_issuer_subject(struct ssl_filter *ssl)
{
    assert(ssl != NULL);

    return ssl->peer_issuer_subject;
}
