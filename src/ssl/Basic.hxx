// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * SSL/TLS initialisation.
 */

#pragma once

#include <openssl/ossl_typ.h>

struct SslConfig;
class SslCtx;

SslCtx
CreateBasicSslCtx(bool server);

void
ApplyServerConfig(SSL_CTX &ssl_ctx, const SslConfig &config);
