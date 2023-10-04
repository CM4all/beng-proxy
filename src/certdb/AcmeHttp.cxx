// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeHttp.hxx"
#include "AcmeChallenge.hxx"
#include "JWS.hxx"
#include "lib/sodium/UrlSafeBase64SHA256.hxx"
#include "io/FileWriter.hxx"
#include "util/SpanCast.hxx"

#include <boost/json.hpp>

#include <sys/stat.h>
#include <unistd.h>

std::string
MakeHttp01(const AcmeChallenge &challenge, EVP_PKEY &account_key)
{
	return challenge.token + "." +
		UrlSafeBase64SHA256(boost::json::serialize(MakeJwk(account_key))).c_str();
}

static void
CreateFile(const char *path, std::span<const std::byte> contents)
{
	FileWriter file(path);

	/* force the file to be world-readable so our web server can
	   deliver it to the ACME server's HTTP client */
	fchmod(file.GetFileDescriptor().Get(), 0644);

	file.Write(contents);
	file.Commit();
}

static void
CreateFile(const char *path, std::string_view contents)
{
	CreateFile(path, AsBytes(contents));
}

[[gnu::pure]]
static bool
IsValidAcmeChallengeToken(const std::string &token) noexcept
{
	return !token.empty() && token.front() != '.' &&
		token.find('/') == token.npos;
}

static std::string
MakeHttp01FilePath(const std::string &directory,
		   const AcmeChallenge &challenge)
{
	if (!IsValidAcmeChallengeToken(challenge.token))
		throw std::runtime_error("Malformed ACME challenge token");

	return directory + "/" + challenge.token;
}

Http01ChallengeFile::Http01ChallengeFile(const std::string &directory,
					 const AcmeChallenge &challenge,
					 EVP_PKEY &account_key)
	:path(MakeHttp01FilePath(directory, challenge))
{
	CreateFile(path.c_str(), MakeHttp01(challenge, account_key));
}

Http01ChallengeFile::~Http01ChallengeFile() noexcept
{
	unlink(path.c_str());
}
