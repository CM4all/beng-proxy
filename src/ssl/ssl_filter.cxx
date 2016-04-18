/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_filter.hxx"
#include "ssl_factory.hxx"
#include "ssl_config.hxx"
#include "ssl_quark.hxx"
#include "Unique.hxx"
#include "Name.hxx"
#include "Error.hxx"
#include "thread_socket_filter.hxx"
#include "pool.hxx"
#include "gerrno.h"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <assert.h>
#include <string.h>

/**
 * Throttle if a #BIO grows larger than this number of bytes.
 */
static constexpr int SSL_THROTTLE_THRESHOLD = 16384;

struct SslFilter final : ThreadSocketFilterHandler {
    /**
     * Buffers which can be accessed from within the thread without
     * holding locks.  These will be copied to/from the according
     * #thread_socket_filter buffers.
     */
    SliceFifoBuffer decrypted_input, plain_output;

    /**
     * Memory BIOs for passing data to/from OpenSSL.
     */
    BIO *const encrypted_input, *const encrypted_output;

    const UniqueSSL ssl;

    bool handshaking = true;

    bool eof;

    AllocatedString<> peer_subject = nullptr, peer_issuer_subject = nullptr;

    SslFilter(UniqueSSL &&_ssl)
        :encrypted_input(BIO_new(BIO_s_mem())),
         encrypted_output(BIO_new(BIO_s_mem())),
         ssl(std::move(_ssl)) {
        decrypted_input.Allocate(fb_pool_get());
        SSL_set_bio(ssl.get(), encrypted_input, encrypted_output);
    }

    ~SslFilter() {
        decrypted_input.Free(fb_pool_get());
        plain_output.FreeIfDefined(fb_pool_get());
    }

    bool Encrypt(GError **error_r);

    /* virtual methods from class ThreadSocketFilterHandler */
    bool Run(ThreadSocketFilter &f, GError **error_r) override;

    void CycleBuffers(ThreadSocketFilter &f) override {
        if (f.IsIdle())
            decrypted_input.CycleIfEmpty(fb_pool_get());
    }

    void Destroy(ThreadSocketFilter &) override {
        this->~SslFilter();
    }
};

static void
ssl_set_error(GError **error_r)
{
    if (error_r == nullptr)
        return;

    unsigned long error = ERR_get_error();
    char buffer[120];
    g_set_error(error_r, ssl_quark(), 0, "%s",
                ERR_error_string(error, buffer));
}

/**
 * Is the #BIO full, i.e. above the #SSL_THROTTLE_THRESHOLD?
 */
gcc_pure
static bool
IsFull(BIO *bio)
{
    return BIO_pending(bio) >= SSL_THROTTLE_THRESHOLD;
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

    if (IsFull(dest))
        /* throttle */
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

static AllocatedString<>
format_subject_name(X509 *cert)
{
    return ToString(X509_get_subject_name(cert));
}

static AllocatedString<>
format_issuer_subject_name(X509 *cert)
{
    return ToString(X509_get_issuer_name(cert));
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

enum class SslDecryptResult {
    SUCCESS,
    ERROR,
    CLOSE_NOTIFY_ALERT,
};

static SslDecryptResult
ssl_decrypt(SSL *ssl, ForeignFifoBuffer<uint8_t> &buffer, GError **error_r)
{
    /* SSL_read() must be called repeatedly until there is no more
       data (or until the buffer is full) */

    while (true) {
        auto w = buffer.Write();
        if (w.IsEmpty())
            return SslDecryptResult::SUCCESS;

        int result = SSL_read(ssl, w.data, w.size);
        if (result <= 0) {
            if (SSL_get_error(ssl, result) == SSL_ERROR_ZERO_RETURN)
                /* got a "close notify" alert from the peer */
                return SslDecryptResult::CLOSE_NOTIFY_ALERT;

            return check_ssl_error(ssl, result, error_r)
                ? SslDecryptResult::SUCCESS
                : SslDecryptResult::ERROR;
        }

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

inline bool
SslFilter::Encrypt(GError **error_r)
{
    return IsFull(encrypted_output) || /* throttle? */
        ssl_encrypt(ssl.get(), plain_output, error_r);
}

/*
 * thread_socket_filter_handler
 *
 */

inline bool
SslFilter::Run(ThreadSocketFilter &f, GError **error_r)
{
    /* copy input (and output to make room for more output) */

    {
        std::unique_lock<std::mutex> lock(f.mutex);

        f.decrypted_input.MoveFrom(decrypted_input);
        plain_output.MoveFromAllowNull(f.plain_output);
        Move(encrypted_input, f.encrypted_input);
        Move(f.encrypted_output, encrypted_output);
    }

    /* let OpenSSL work */

    ERR_clear_error();

    if (gcc_unlikely(handshaking)) {
        int result = SSL_do_handshake(ssl.get());
        if (result == 1) {
            handshaking = false;

            UniqueX509 cert(SSL_get_peer_certificate(ssl.get()));
            if (cert != nullptr) {
                peer_subject = format_subject_name(cert.get());
                peer_issuer_subject = format_issuer_subject_name(cert.get());
            }
        } else if (!check_ssl_error(ssl.get(), result, error_r))
            return false;
    }

    if (gcc_likely(!handshaking)) {
        if (!Encrypt(error_r))
            return false;

        switch (ssl_decrypt(ssl.get(), decrypted_input, error_r)) {
        case SslDecryptResult::SUCCESS:
            break;

        case SslDecryptResult::ERROR:
            return false;

        case SslDecryptResult::CLOSE_NOTIFY_ALERT:
            {
                std::unique_lock<std::mutex> lock(f.mutex);
                f.input_eof = true;
            }
            break;
        }
    }

    /* copy output */

    {
        std::unique_lock<std::mutex> lock(f.mutex);

        f.decrypted_input.MoveFrom(decrypted_input);

        /* let the main thread free our plain_output buffer */
        plain_output.SwapIfNull(f.plain_output);

        Move(f.encrypted_output, encrypted_output);
        f.drained = plain_output.IsEmpty() &&
            BIO_eof(encrypted_output);
    }

    return true;
}

/*
 * constructor
 *
 */

SslFilter *
ssl_filter_new(struct pool &pool, UniqueSSL &&ssl)
{
    return NewFromPool<SslFilter>(pool, std::move(ssl));
}

SslFilter *
ssl_filter_new(struct pool *pool, SslFactory &factory,
               GError **error_r)
{
    assert(pool != nullptr);

    try {
        return NewFromPool<SslFilter>(*pool, ssl_factory_make(factory));
    } catch (const SslError &e) {
        g_set_error(error_r, ssl_quark(), 0, "SSL_new() failed: %s",
                    e.what());
        return nullptr;
    }
}

ThreadSocketFilterHandler &
ssl_filter_get_handler(SslFilter &ssl)
{
    return ssl;
}

const char *
ssl_filter_get_peer_subject(SslFilter *ssl)
{
    assert(ssl != nullptr);

    return ssl->peer_subject.c_str();
}

const char *
ssl_filter_get_peer_issuer_subject(SslFilter *ssl)
{
    assert(ssl != nullptr);

    return ssl->peer_issuer_subject.c_str();
}
