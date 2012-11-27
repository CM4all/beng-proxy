/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_filter.h"
#include "ssl_config.h"
#include "notify.h"
#include "pool.h"
#include "fifo-buffer.h"
#include "buffered_io.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

struct ssl_filter {
    struct pool *pool;

    struct notify *notify;

    int encrypted_fd, plain_fd;

    struct fifo_buffer *from_encrypted, *from_plain;

    SSL *ssl;

    const char *peer_subject, *peer_issuer_subject;

    pthread_mutex_t mutex;
    pthread_t thread;

    bool closing;
};

static inline void
ssl_filter_lock(struct ssl_filter *ssl)
{
    pthread_mutex_lock(&ssl->mutex);
}

static inline void
ssl_filter_unlock(struct ssl_filter *ssl)
{
    pthread_mutex_unlock(&ssl->mutex);
}

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
 * Close both sockets.
 *
 * The #ssl_filter object must be locked by the caller.
 */
static void
ssl_filter_close_sockets(struct ssl_filter *ssl)
{
    ssl->closing = true;

    if (ssl->encrypted_fd >= 0) {
        close(ssl->encrypted_fd);
        ssl->encrypted_fd = -1;
    }

    if (ssl->plain_fd >= 0) {
        close(ssl->plain_fd);
        ssl->plain_fd = -1;
    }
}

/**
 * Shut down both sockets.  This is used to wake up the thread; note
 * that closing the file descriptors will not make poll() return.
 *
 * The #ssl_filter object must be locked by the caller.
 */
static void
ssl_filter_shutdown_sockets(struct ssl_filter *ssl)
{
    ssl->closing = true;

    if (ssl->encrypted_fd >= 0)
        shutdown(ssl->encrypted_fd, SHUT_RDWR);

    if (ssl->plain_fd >= 0)
        shutdown(ssl->plain_fd, SHUT_RDWR);
}

/**
 * Wait for events on the encrypted socket.
 *
 * The #ssl_filter object must be locked by the caller.
 */
static short
ssl_poll(struct ssl_filter *ssl, short events, int timeout_ms,
         GError **error_r)
{
    struct pollfd pfd = {
        .fd = ssl->encrypted_fd,
        .events = events,
    };

    ssl_filter_unlock(ssl);
    int ret = poll(&pfd, 1, timeout_ms);
    ssl_filter_lock(ssl);
    if (ssl->closing)
        return 0;
    else if (ret > 0)
        return pfd.revents;
    else if (ret == 0) {
        g_set_error(error_r, ssl_quark(), 0, "Timeout");
        return 0;
    } else {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "poll() failed: %s", strerror(errno));
        return 0;
    }
}

static bool
ssl_filter_do_handshake(struct ssl_filter *ssl, GError **error_r)
{
    while (!ssl->closing) {
        assert(ssl->encrypted_fd >= 0);

        ERR_clear_error();
        int ret = SSL_do_handshake(ssl->ssl);
        if (ret == 1)
            return true;

        if (ret == 0) {
            ssl_set_error(error_r);
            return false;
        }

        int error = SSL_get_error(ssl->ssl, ret);
        if (error == SSL_ERROR_WANT_READ) {
            if (ssl_poll(ssl, POLLIN, -1, error_r) == 0)
                return false;
        } else if (error == SSL_ERROR_WANT_WRITE) {
            if (ssl_poll(ssl, POLLOUT, -1, error_r) == 0)
                return false;
        } else {
            ssl_set_error(error_r);
            return false;
        }
    }

    g_set_error(error_r, ssl_quark(), 0, "Closed");
    return false;
}

static char *
format_name(struct pool *pool, X509_NAME *name)
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

    return p_strndup(pool, buffer, length);
}

static char *
format_subject_name(struct pool *pool, X509 *cert)
{
    return format_name(pool, X509_get_subject_name(cert));
}

static char *
format_issuer_subject_name(struct pool *pool, X509 *cert)
{
    return format_name(pool, X509_get_issuer_name(cert));
}

gcc_const
static bool
is_ssl_error(SSL *ssl, int ret)
{
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

/**
 * @return false on error
 */
static bool
ssl_decrypt(SSL *ssl, struct fifo_buffer *buffer)
{
    size_t length;
    void *data = fifo_buffer_write(buffer, &length);
    if (buffer == NULL)
        return true;

    ERR_clear_error();
    int ret = SSL_read(ssl, data, length);
    if (ret > 0) {
        fifo_buffer_append(buffer, ret);
        return true;
    } else
        return ret < 0 && !is_ssl_error(ssl, ret);
}

/**
 * @return false on error
 */
static bool
ssl_encrypt(SSL *ssl, struct fifo_buffer *buffer)
{
    size_t length;
    const void *data = fifo_buffer_read(buffer, &length);
    if (buffer == NULL)
        return true;

    ERR_clear_error();
    int ret = SSL_write(ssl, data, length);
    if (ret > 0) {
        fifo_buffer_consume(buffer, ret);
        return true;
    } else
        return ret < 0 && !is_ssl_error(ssl, ret);
}

static void *
ssl_filter_thread(void *ctx)
{
    struct ssl_filter *ssl = ctx;

    ssl_filter_lock(ssl);

    GError *error = NULL;
    if (!ssl_filter_do_handshake(ssl, &error)) {
        if (error != NULL) {
            fprintf(stderr, "%s\n", error->message);
            g_error_free(error);
        } else {
            assert(ssl->closing);
        }

        ssl_filter_close_sockets(ssl);
    }

    if (!ssl->closing) {
        X509 *cert = SSL_get_peer_certificate(ssl->ssl);
        if (cert != NULL)
            ssl->peer_subject = format_subject_name(ssl->pool, cert);
        if (cert != NULL)
            ssl->peer_issuer_subject =
                format_issuer_subject_name(ssl->pool, cert);
    }

    struct pollfd pfds[2] = {
        [0] = {
            .fd = ssl->encrypted_fd,
        },
        [1] = {
            .fd = ssl->plain_fd,
        },
    };

    while (!ssl->closing) {
        assert(ssl->encrypted_fd >= 0);
        assert(ssl->plain_fd >= 0);

        pfds[0].events = pfds[1].events = 0;
        pfds[0].revents = pfds[1].revents = 0;

        if (!fifo_buffer_full(ssl->from_encrypted))
            pfds[0].events |= POLLIN;

        if (!fifo_buffer_empty(ssl->from_encrypted))
            pfds[1].events |= POLLOUT;

        if (!fifo_buffer_full(ssl->from_plain))
            pfds[1].events |= POLLIN;

        if (!fifo_buffer_empty(ssl->from_plain))
            pfds[0].events |= POLLOUT;

        struct pollfd *p;
        nfds_t nfds;
        if (pfds[0].events != 0) {
            p = pfds;
            nfds = pfds[1].events != 0 ? 2 : 1;
        } else {
            assert(pfds[1].events != 0);
            p = pfds + 1;
            nfds = 1;
        }

        ssl_filter_unlock(ssl);
        int n = poll(p, nfds, -1);
        ssl_filter_lock(ssl);
        if (n <= 0 || ssl->closing)
            break;

        if ((pfds[1].revents & POLLIN) != 0) {
            const bool was_empty = fifo_buffer_empty(ssl->from_plain);

            ssize_t nbytes = recv_to_buffer(ssl->plain_fd, ssl->from_plain, 65536);
            assert(nbytes != -2);

            if (nbytes == 0 || (nbytes < 0 && errno != EAGAIN)) {
                close(ssl->plain_fd);
                ssl->plain_fd = -1;
                break;
            }

            if (was_empty && !fifo_buffer_empty(ssl->from_plain))
                /* try decryption again to see if OpenSSL has waits
                   for more encrypted data from us */
                pfds[0].revents |= POLLOUT;
        }

        if ((pfds[1].revents & POLLOUT) != 0) {
            const bool was_full = fifo_buffer_full(ssl->from_encrypted);

            if (send_from_buffer(ssl->plain_fd, ssl->from_encrypted) < 0 &&
                errno != EAGAIN) {
                close(ssl->plain_fd);
                ssl->plain_fd = -1;
                break;
            }

            if (was_full && !fifo_buffer_full(ssl->from_encrypted))
                /* try decryption again to see if OpenSSL has more
                   decrypted data for us */
                pfds[0].revents |= POLLIN;
        }

        if ((pfds[0].revents & POLLIN) != 0) {
            assert(!fifo_buffer_full(ssl->from_encrypted));

            if (!ssl_decrypt(ssl->ssl, ssl->from_encrypted)) {
                close(ssl->encrypted_fd);
                ssl->encrypted_fd = -1;
                break;
            }
        }

        if ((pfds[0].revents & POLLOUT) != 0) {
            assert(!fifo_buffer_empty(ssl->from_plain));

            if (!ssl_encrypt(ssl->ssl, ssl->from_plain)) {
                close(ssl->encrypted_fd);
                ssl->encrypted_fd = -1;
                break;
            }
        }
    }

    ssl_filter_close_sockets(ssl);
    ssl_filter_unlock(ssl);

    ERR_clear_error();

    notify_signal(ssl->notify);
    return NULL;
}

struct ssl_filter *
ssl_filter_new(struct pool *pool, SSL_CTX *ssl_ctx,
               int encrypted_fd, int plain_fd,
               struct notify *notify,
               GError **error_r)
{
    assert(pool != NULL);
    assert(ssl_ctx != NULL);

    struct ssl_filter *ssl = p_malloc(pool, sizeof(*ssl));
    ssl->pool = pool;
    ssl->notify = notify;
    ssl->encrypted_fd = encrypted_fd;
    ssl->plain_fd = plain_fd;

    ssl->from_encrypted = fifo_buffer_new(pool, 4096);
    ssl->from_plain = fifo_buffer_new(pool, 4096);

    ssl->ssl = SSL_new(ssl_ctx);
    if (ssl->ssl == NULL) {
        g_set_error(error_r, ssl_quark(), 0, "SSL_new() failed");
        return NULL;
    }

    SSL_set_accept_state(ssl->ssl);
    SSL_set_fd(ssl->ssl, encrypted_fd);

    pthread_mutex_init(&ssl->mutex, NULL);

    ssl->peer_subject = NULL;
    ssl->peer_issuer_subject = NULL;
    ssl->closing = false;

    int error = pthread_create(&ssl->thread, NULL, ssl_filter_thread, ssl);
    if (error != 0) {
        SSL_free(ssl->ssl);
        g_set_error(error_r, ssl_quark(), error,
                    "Failed to create thread: %s", strerror(error));
        return NULL;
    }

    return ssl;
}

void
ssl_filter_free(struct ssl_filter *ssl)
{
    assert(ssl != NULL);

    ssl_filter_lock(ssl);

    if (ssl->ssl != NULL)
        SSL_free(ssl->ssl);

    ssl_filter_shutdown_sockets(ssl);

    ssl_filter_unlock(ssl);

    pthread_join(ssl->thread, NULL);
    pthread_mutex_destroy(&ssl->mutex);

    ssl_filter_close_sockets(ssl);
}

const char *
ssl_filter_get_peer_subject(struct ssl_filter *ssl)
{
    assert(ssl != NULL);

    ssl_filter_lock(ssl);
    const char *peer_subject = ssl->peer_subject;
    ssl_filter_unlock(ssl);

    return peer_subject;
}

const char *
ssl_filter_get_peer_issuer_subject(struct ssl_filter *ssl)
{
    assert(ssl != NULL);

    ssl_filter_lock(ssl);
    const char *subject = ssl->peer_issuer_subject;
    ssl_filter_unlock(ssl);

    return subject;
}
