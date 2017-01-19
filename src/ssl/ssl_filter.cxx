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
#include "FifoBufferBio.hxx"
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

struct SslFilter final : ThreadSocketFilterHandler {
    /**
     * Buffers which can be accessed from within the thread without
     * holding locks.  These will be copied to/from the according
     * #thread_socket_filter buffers.
     */
    SliceFifoBuffer encrypted_input, decrypted_input,
        plain_output, encrypted_output;

    const UniqueSSL ssl;

    bool handshaking = true;

    AllocatedString<> peer_subject = nullptr, peer_issuer_subject = nullptr;

    SslFilter(UniqueSSL &&_ssl)
        :ssl(std::move(_ssl)) {
        SSL_set_bio(ssl.get(),
                    NewFifoBufferBio(encrypted_input),
                    NewFifoBufferBio(encrypted_output));
    }

    ~SslFilter() {
        encrypted_input.FreeIfDefined(fb_pool_get());
        decrypted_input.FreeIfDefined(fb_pool_get());
        plain_output.FreeIfDefined(fb_pool_get());
        encrypted_output.FreeIfDefined(fb_pool_get());
    }

    bool Encrypt(GError **error_r);

    /* virtual methods from class ThreadSocketFilterHandler */
    void PreRun(ThreadSocketFilter &f) override;
    bool Run(ThreadSocketFilter &f, GError **error_r) override;
    void PostRun(ThreadSocketFilter &f) override;

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

    /**
     * More encrypted_input data is required.
     */
    MORE,

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
        if (result < 0 && SSL_get_error(ssl, result) == SSL_ERROR_WANT_READ)
            return SslDecryptResult::MORE;

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
    return ssl_encrypt(ssl.get(), plain_output, error_r);
}

/*
 * thread_socket_filter_handler
 *
 */

void
SslFilter::PreRun(ThreadSocketFilter &f)
{
    if (f.IsIdle()) {
        decrypted_input.AllocateIfNull(fb_pool_get());
        encrypted_output.AllocateIfNull(fb_pool_get());
    }
}

bool
SslFilter::Run(ThreadSocketFilter &f, GError **error_r)
{
    /* copy input (and output to make room for more output) */

    {
        std::unique_lock<std::mutex> lock(f.mutex);

        if (f.decrypted_input.IsNull() || f.encrypted_output.IsNull()) {
            /* retry, let PreRun() allocate the missing buffer */
            f.again = true;
            return true;
        }

        f.decrypted_input.MoveFromAllowNull(decrypted_input);

        plain_output.MoveFromAllowNull(f.plain_output);
        encrypted_input.MoveFromAllowSrcNull(f.encrypted_input);
        f.encrypted_output.MoveFromAllowNull(encrypted_output);

        if (decrypted_input.IsNull() || encrypted_output.IsNull()) {
            /* retry, let PreRun() allocate the missing buffer */
            f.again = true;
            return true;
        }
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
        } else if (!check_ssl_error(ssl.get(), result, error_r)) {
            /* flush the encrypted_output buffer, because it may
               contain a "TLS alert" */
            f.encrypted_output.MoveFromAllowNull(encrypted_output);
            return false;
        }
    }

    if (gcc_likely(!handshaking)) {
        if (!Encrypt(error_r))
            return false;

        switch (ssl_decrypt(ssl.get(), decrypted_input, error_r)) {
        case SslDecryptResult::SUCCESS:
            break;

        case SslDecryptResult::MORE:
            if (encrypted_input.IsDefinedAndFull()) {
                g_set_error_literal(error_r, ssl_quark(), 0,
                                    "SSL encrypted_input buffer is full");
                return false;
            }

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

        f.decrypted_input.MoveFromAllowNull(decrypted_input);
        f.encrypted_output.MoveFromAllowNull(encrypted_output);
        f.drained = plain_output.IsEmpty() && encrypted_output.IsEmpty();

        if (!f.plain_output.IsEmpty() && !plain_output.IsDefinedAndFull() &&
            !encrypted_output.IsDefinedAndFull())
            /* there's more data, and we're ready to handle it: try
               again */
            f.again = true;

        f.handshaking = handshaking;
    }

    return true;
}

void
SslFilter::PostRun(ThreadSocketFilter &f)
{
    if (f.IsIdle()) {
        plain_output.FreeIfEmpty(fb_pool_get());
        encrypted_input.FreeIfEmpty(fb_pool_get());
        decrypted_input.FreeIfEmpty(fb_pool_get());
        encrypted_output.FreeIfEmpty(fb_pool_get());
    }
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
