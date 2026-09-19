// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <openssl/ossl_typ.h>

#include <set>
#include <string>

/**
 * Collect all host names of the given certificate (subject alt names
 * and common name).
 */
[[gnu::pure]]
std::set<std::string, std::less<>>
AllNames(const X509 &cert) noexcept;

/**
 * Check if the specified certificate has only the requested names.
 *
 * Throws on error.
 */
void
CheckCertificateNames(X509 &cert,
		      const std::set<std::string, std::less<>> &requested_names);
