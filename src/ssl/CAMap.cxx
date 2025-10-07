// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CAMap.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/LoadFile.hxx"

#include <fmt/format.h>

#include <cassert>

using std::string_view_literals::operator""sv;

void
CAMap::LoadChainFile(const char *path)
{
	auto chain = LoadCertChainFile(path);
	assert(!chain.empty());

	const X509_NAME *subject = X509_get_subject_name(chain.front().get());
	if (subject == nullptr)
		throw SslError{fmt::format("Certificate has no subject: {}"sv, path)};

	auto digest = CalcSHA1(*subject);
	auto r = map.emplace(std::move(digest), std::move(chain));
	if (!r.second)
		throw SslError{fmt::format("Duplicate certificate: {}"sv, path)};
}

const CAMap::Chain *
CAMap::Find(const X509_NAME &subject) const noexcept
{
	if (const auto i = map.find(CalcSHA1(subject)); i != map.end())
		return &i->second;

	return nullptr;
}

const CAMap::Chain *
CAMap::FindIssuer(const X509 &cert) const noexcept
{
	if (const X509_NAME *issuer = X509_get_issuer_name(&cert);
	    issuer != nullptr)
		return Find(*issuer);

	return nullptr;
}
