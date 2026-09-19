// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Names.hxx"
#include "AcmeUtil.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/AltName.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/openssl/Name.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/x509.h>

std::set<std::string, std::less<>>
AllNames(const X509 &cert) noexcept
{
	std::set<std::string, std::less<>> result;

	for (auto &i : GetSubjectAltNames(cert))
		if (!IsAcmeInvalid(i))
			/* ignore "*.acme.invalid" */
			result.emplace(std::move(i));

	const auto cn = GetCommonName(cert);
	if (cn != nullptr)
		result.emplace(cn.c_str());

	return result;
}

void
CheckCertificateNames(X509 &cert,
		      const std::set<std::string, std::less<>> &requested_names)
{
	for (const auto &name : AllNames(cert))
		if (!requested_names.contains(name))
			throw FmtRuntimeError("The issued certificate contains a name which was not requested: {}",
					      name);
}
