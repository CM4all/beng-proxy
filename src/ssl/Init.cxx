// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Init.hxx"
#include "CompletionHandler.hxx"
#include "FifoBufferBio.hxx"

#include <openssl/ssl.h>

void
ssl_global_init()
{
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS|
			 OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
			 nullptr);

	InitSslCompletionHandler();
}

void
ssl_global_deinit() noexcept
{
	DeinitFifoBufferBio();
}
