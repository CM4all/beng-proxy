// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/* JSON Web Signature library */

#pragma once

#include <openssl/ossl_typ.h>

#include <boost/json/fwd.hpp>

#include <string>

/**
 * Throws on error.
 */
boost::json::object
MakeJwk(EVP_PKEY &key);
