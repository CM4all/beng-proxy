// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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

	/**
	 * Was this object permanently cancelled?  This flag is
	 * necessary to fix a race between SetCancellable() in a
	 * worker thread and CheckCancel() in the main thread.
	 */
	bool already_cancelled = false;

public:
	~SslCompletionHandler() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	struct AlreadyCancelled {};

	/**
	 * Throws #AlreadyCancelled if this object was already
	 * cancelled.
	 */
	void SetCancellable(Cancellable &cancellable) {
		const std::scoped_lock lock{mutex};

		assert(!cancel_ptr);

		if (already_cancelled)
			throw AlreadyCancelled{};

		cancel_ptr = cancellable;
	}

	void InvokeSslCompletion() noexcept {
		/* no mutex lock here because completion runs in the
		   main thread and thus will never race with
		   cancellation (also in the main thread) */

		assert(cancel_ptr);
		assert(!already_cancelled);
		cancel_ptr = nullptr;

		OnSslCompletion();
	}

protected:
	CancellablePointer LockSteal() noexcept {
		const std::scoped_lock lock{mutex};
		already_cancelled = true;
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
