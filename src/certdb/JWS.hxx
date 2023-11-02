// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/* JSON Web Signature library */

#pragma once

#include <openssl/ossl_typ.h>

#include <nlohmann/json_fwd.hpp>

/**
 * Throws on error.
 */
nlohmann::json
MakeJwk(EVP_PKEY &key);
