/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Filter.hxx"
#include "CompletionHandler.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/UniqueX509.hxx"
#include "FifoBufferBio.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "util/AllocatedArray.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <assert.h>
#include <string.h>

class SslFilter final : public ThreadSocketFilterHandler,
			SslCompletionHandler {
	/**
	 * Buffers which can be accessed from within the thread without
	 * holding locks.  These will be copied to/from the according
	 * #thread_socket_filter buffers.
	 */
	SliceFifoBuffer encrypted_input, decrypted_input,
		plain_output, encrypted_output;

	const UniqueSSL ssl;

	bool handshaking = true;

	AllocatedArray<unsigned char> alpn_selected;

public:
	AllocatedString peer_subject, peer_issuer_subject;

	SslFilter(UniqueSSL &&_ssl)
		:ssl(std::move(_ssl)) {
		SSL_set_bio(ssl.get(),
			    NewFifoBufferBio(encrypted_input),
			    NewFifoBufferBio(encrypted_output));

		SetSslCompletionHandler(*ssl, *this);
	}

	std::span<const unsigned char> GetAlpnSelected() const noexcept {
		return {alpn_selected.data(), alpn_selected.size()};
	}

private:
	/**
	 * Called from inside Run() right after the handshake has
	 * completed.  This is used to collect some data for our
	 * public getters.
	 */
	void PostHandshake() noexcept;

	void Encrypt();

	/* virtual methods from class ThreadSocketFilterHandler */
	void PreRun(ThreadSocketFilterInternal &f) noexcept override;
	void Run(ThreadSocketFilterInternal &f) override;
	void PostRun(ThreadSocketFilterInternal &f) noexcept override;
	void CancelRun(ThreadSocketFilterInternal &f) noexcept override;

	/* virtual methods from class SslCompletionHandler */
	void OnSslCompletion() noexcept override {
		ScheduleRun();
	}
};

static AllocatedString
format_subject_name(X509 *cert)
{
	return ToString(X509_get_subject_name(cert));
}

static AllocatedString
format_issuer_subject_name(X509 *cert)
{
	return ToString(X509_get_issuer_name(cert));
}

static constexpr bool
IsSslError(int error) noexcept
{
	switch (error) {
	case SSL_ERROR_NONE:
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
	case SSL_ERROR_WANT_CONNECT:
	case SSL_ERROR_WANT_ACCEPT:
	case SSL_ERROR_WANT_X509_LOOKUP:
		return false;

	default:
		return true;
	}
}

inline void
SslFilter::PostHandshake() noexcept
{
	const unsigned char *alpn_data;
	unsigned int alpn_length;
	SSL_get0_alpn_selected(ssl.get(), &alpn_data, &alpn_length);
	if (alpn_length > 0)
		alpn_selected = std::span<const unsigned char>(alpn_data,
							       alpn_length);

	UniqueX509 cert(SSL_get_peer_certificate(ssl.get()));
	if (cert != nullptr) {
		peer_subject = format_subject_name(cert.get());
		peer_issuer_subject = format_issuer_subject_name(cert.get());
	}
}

enum class SslDecryptResult {
	SUCCESS,

	/**
	 * More encrypted_input data is required.
	 */
	MORE,

	CLOSE_NOTIFY_ALERT,
};

static SslDecryptResult
ssl_decrypt(SSL *ssl, ForeignFifoBuffer<std::byte> &buffer)
{
	/* SSL_read() must be called repeatedly until there is no more
	   data (or until the buffer is full) */

	while (true) {
		auto w = buffer.Write();
		if (w.empty())
			return SslDecryptResult::SUCCESS;

		size_t nbytes;
		int result = SSL_read_ex(ssl, w.data(), w.size(), &nbytes);
		if (!result) {
			const int error = SSL_get_error(ssl, result);
			if (error == SSL_ERROR_WANT_READ)
				return SslDecryptResult::MORE;

			if (error == SSL_ERROR_ZERO_RETURN)
				/* got a "close notify" alert from the peer */
				return SslDecryptResult::CLOSE_NOTIFY_ALERT;

			if (IsSslError(error))
				throw SslError{};

			return SslDecryptResult::SUCCESS;
		}

		buffer.Append(nbytes);
	}
}

static void
ssl_encrypt(SSL *ssl, ForeignFifoBuffer<std::byte> &buffer)
{
	/* SSL_write() must be called repeatedly until there is no more
	   data; with SSL_MODE_ENABLE_PARTIAL_WRITE, SSL_write() finishes
	   only the current incomplete record, and additional data which
	   has been submitted more recently will only be considered in the
	   next SSL_write() call */

	while (true) {
		auto r = buffer.Read();
		if (r.empty())
			return;

		size_t nbytes;
		int result = SSL_write_ex(ssl, r.data(), r.size(), &nbytes);
		if (!result) {
			const int error = SSL_get_error(ssl, result);
			if (IsSslError(error))
				throw SslError{};

			return;
		}

		buffer.Consume(nbytes);
	}
}

inline void
SslFilter::Encrypt()
{
	ssl_encrypt(ssl.get(), plain_output);
}

/*
 * thread_socket_filter_handler
 *
 */

void
SslFilter::PreRun(ThreadSocketFilterInternal &f) noexcept
{
	if (f.IsIdle()) {
		decrypted_input.AllocateIfNull(fb_pool_get());
		encrypted_output.AllocateIfNull(fb_pool_get());
	}
}

void
SslFilter::Run(ThreadSocketFilterInternal &f)
{
	/* copy input (and output to make room for more output) */

	{
		const std::scoped_lock lock{f.mutex};

		if (f.decrypted_input.IsNull() || f.encrypted_output.IsNull()) {
			/* retry, let PreRun() allocate the missing buffer */
			f.again = true;
			return;
		}

		f.decrypted_input.MoveFromAllowNull(decrypted_input);

		plain_output.MoveFromAllowNull(f.plain_output);
		encrypted_input.MoveFromAllowSrcNull(f.encrypted_input);

		f.encrypted_output.MoveFromAllowNull(encrypted_output);

		if (decrypted_input.IsNull() || encrypted_output.IsNull()) {
			/* retry, let PreRun() allocate the missing buffer */
			f.again = true;
			return;
		}
	}

	/* let OpenSSL work */

	ERR_clear_error();

	if (handshaking) [[unlikely]] {
		int result = SSL_do_handshake(ssl.get());
		if (result == 1) {
			handshaking = false;
			PostHandshake();
		} else if (const int error = SSL_get_error(ssl.get(), result);
			   IsSslError(error)) {
			{
				/* flush the encrypted_output buffer, because it may
				   contain a "TLS alert" */
				const std::scoped_lock lock{f.mutex};
				f.encrypted_output.MoveFromAllowNull(encrypted_output);
			}

			throw SslError{};
		}
	}

	if (!handshaking) [[likely]] {
		Encrypt();

		switch (ssl_decrypt(ssl.get(), decrypted_input)) {
		case SslDecryptResult::SUCCESS:
			break;

		case SslDecryptResult::MORE:
			if (encrypted_input.IsDefinedAndFull())
				throw std::runtime_error("SSL encrypted_input buffer is full");

			break;

		case SslDecryptResult::CLOSE_NOTIFY_ALERT:
			{
				const std::scoped_lock lock(f.mutex);
				f.input_eof = true;
			}
			break;
		}
	}

	/* copy output */

	{
		const std::scoped_lock lock{f.mutex};

		f.decrypted_input.MoveFromAllowNull(decrypted_input);
		f.encrypted_output.MoveFromAllowNull(encrypted_output);
		f.drained = plain_output.empty() && encrypted_output.empty();

		if (!decrypted_input.IsDefinedAndFull() && !f.encrypted_input.empty())
			/* there's more data to be decrypted and we
			   still have room in the destination buffer,
			   so let's run again */
			f.again = true;

		if (!f.plain_output.empty() && !plain_output.IsDefinedAndFull() &&
		    !encrypted_output.IsDefinedAndFull())
			/* there's more data, and we're ready to handle it: try
			   again */
			f.again = true;

		f.handshaking = handshaking;
	}
}

void
SslFilter::PostRun(ThreadSocketFilterInternal &f) noexcept
{
	if (f.IsIdle()) {
		plain_output.FreeIfEmpty();
		encrypted_input.FreeIfEmpty();
		decrypted_input.FreeIfEmpty();
		encrypted_output.FreeIfEmpty();
	}
}

void
SslFilter::CancelRun(ThreadSocketFilterInternal &) noexcept
{
	if (cancel_ptr)
		/* cancel the CertCache::Apply() call */
		cancel_ptr.Cancel();
}

/*
 * constructor
 *
 */

std::unique_ptr<ThreadSocketFilterHandler>
ssl_filter_new(UniqueSSL &&ssl) noexcept
{
	return std::make_unique<SslFilter>(std::move(ssl));
}

SslFilter &
ssl_filter_cast_from(ThreadSocketFilterHandler &tsfh) noexcept
{
	return static_cast<SslFilter &>(tsfh);
}

const SslFilter *
ssl_filter_cast_from(const SocketFilter *socket_filter) noexcept
{
	const auto *tsf = dynamic_cast<const ThreadSocketFilter *>(socket_filter);
	if (tsf == nullptr)
		return nullptr;

	return dynamic_cast<const SslFilter *>(&tsf->GetHandler());
}

std::span<const unsigned char>
ssl_filter_get_alpn_selected(const SslFilter &ssl) noexcept
{
	return ssl.GetAlpnSelected();
}

const char *
ssl_filter_get_peer_subject(const SslFilter &ssl) noexcept
{
	return ssl.peer_subject.c_str();
}

const char *
ssl_filter_get_peer_issuer_subject(const SslFilter &ssl) noexcept
{
	return ssl.peer_issuer_subject.c_str();
}
