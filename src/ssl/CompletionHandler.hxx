/*
 * Copyright 2007-2022 CM4all GmbH
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

#pragma once

#include "util/Cancellable.hxx"

#include <openssl/ossl_typ.h>

#include <cassert>
#include <mutex>

/**
 * Handler for the completion of a suspended OpenSSL callback.
 */
class SslCompletionHandler {
	/**
	 * A global mutex which protects all #SslCompletionHandler
	 * instances.  Suspended OpenSSL callbacks are rare enough
	 * that one global mutex should do.
	 */
	static inline std::mutex mutex;

	CancellablePointer cancel_ptr;

public:
	~SslCompletionHandler() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	void SetCancellable(Cancellable &cancellable) noexcept {
		const std::scoped_lock lock{mutex};

		assert(!cancel_ptr);

		cancel_ptr = cancellable;
	}

	void InvokeSslCompletion() noexcept {
		/* no mutex lock here because completion runs in the
		   main thread and thus will never race with
		   cancellation (also in the main thread) */

		assert(cancel_ptr);
		cancel_ptr = nullptr;

		OnSslCompletion();
	}

protected:
	CancellablePointer LockSteal() noexcept {
		const std::scoped_lock lock{mutex};
		return std::move(cancel_ptr);
	}

	void CheckCancel() noexcept {
		if (auto c = LockSteal())
			c.Cancel();
	}

	/**
	 * A suspended callback is complete, and the #SSL object can
	 * continue to work.
	 */
	virtual void OnSslCompletion() noexcept = 0;
};

void
InitSslCompletionHandler();

void
SetSslCompletionHandler(SSL &ssl, SslCompletionHandler &handler) noexcept;

[[gnu::pure]]
SslCompletionHandler &
GetSslCompletionHandler(SSL &ssl) noexcept;

void
InvokeSslCompletionHandler(SSL &ssl) noexcept;
