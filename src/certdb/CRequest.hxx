// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/openssl/Edit.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/GeneralName.hxx"
#include "lib/openssl/UniqueX509.hxx"

static UniqueX509_NAME
MakeCommonName(const char *common_name)
{
	UniqueX509_NAME n(X509_NAME_new());
	if (n == nullptr)
		throw "X509_NAME_new() failed";

	X509_NAME_add_entry_by_NID(n.get(), NID_commonName, MBSTRING_ASC,
				   const_cast<unsigned char *>((const unsigned char *)common_name),
				   -1, -1, 0);
	return n;
}

static void
CopyCommonName(X509_REQ &req, X509 &src)
{
	X509_NAME *src_subject = X509_get_subject_name(&src);
	if (src_subject == nullptr)
		return;

	int i = X509_NAME_get_index_by_NID(src_subject, NID_commonName, -1);
	if (i < 0)
		return;

	auto *common_name = X509_NAME_get_entry(src_subject, i);
	auto *dest_subject = X509_REQ_get_subject_name(&req);
	X509_NAME_add_entry(dest_subject, common_name, -1, 0);
}

/**
 * Add a subject_alt_name extension for each host name in the list.
 */
static void
AddDnsAltNames(X509_REQ &req, const auto &hosts)
{
	OpenSSL::UniqueGeneralNames ns;
	for (const auto &host : hosts)
		ns.push_back(OpenSSL::ToDnsName(host.c_str()));

	AddAltNames(req, ns);
}

/**
 * Copy the subject_alt_name extension from the source certificate to
 * the request.
 */
static void
CopyDnsAltNames(X509_REQ &req, X509 &src)
{
	int i = X509_get_ext_by_NID(&src, NID_subject_alt_name, -1);
	if (i < 0)
		/* no subject_alt_name found, no-op */
		return;

	auto ext = X509_get_ext(&src, i);
	if (ext == nullptr)
		return;

	OpenSSL::UniqueGeneralNames gn(reinterpret_cast<GENERAL_NAMES *>(X509V3_EXT_d2i(ext)));
	if (!gn)
		return;

	AddAltNames(req, gn);
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, const char *common_name, const auto &alt_hosts)
{
	UniqueX509_REQ req(X509_REQ_new());
	if (req == nullptr)
		throw "X509_REQ_new() failed";

	if (common_name != nullptr)
		X509_REQ_set_subject_name(req.get(), MakeCommonName(common_name).get());

	if (!alt_hosts.empty())
		AddDnsAltNames(*req, alt_hosts);

	X509_REQ_set_pubkey(req.get(), &key);

	if (!X509_REQ_sign(req.get(), &key, EVP_sha256()))
		throw SslError("X509_REQ_sign() failed");

	return req;
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, X509 &src)
{
	UniqueX509_REQ req(X509_REQ_new());
	if (req == nullptr)
		throw "X509_REQ_new() failed";

	CopyCommonName(*req, src);
	CopyDnsAltNames(*req, src);

	X509_REQ_set_pubkey(req.get(), &key);

	if (!X509_REQ_sign(req.get(), &key, EVP_sha256()))
		throw SslError("X509_REQ_sign() failed");

	return req;
}
