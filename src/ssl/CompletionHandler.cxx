// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CompletionHandler.hxx"
#include "lib/openssl/Error.hxx"

#include <openssl/ssl.h>
#include <openssl/err.h>


static int ssl_completion_handler_index;

void
InitSslCompletionHandler()
{
	ERR_clear_error();

	ssl_completion_handler_index =
		SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
	if (ssl_completion_handler_index < 0)
		throw SslError("SSL_get_ex_new_index() failed");
}

void
SetSslCompletionHandler(SSL &ssl, SslCompletionHandler &handler) noexcept
{
	SSL_set_ex_data(&ssl, ssl_completion_handler_index, &handler);
}

SslCompletionHandler &
GetSslCompletionHandler(SSL &ssl) noexcept
{
	auto *handler = (SslCompletionHandler *)
		SSL_get_ex_data(&ssl, ssl_completion_handler_index);
	assert(handler != nullptr);
	return *handler;
}

void
InvokeSslCompletionHandler(SSL &ssl) noexcept
{
	auto &handler = GetSslCompletionHandler(ssl);
	handler.InvokeSslCompletion();
}
