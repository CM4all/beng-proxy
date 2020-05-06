/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Request.hxx"
#include "Instance.hxx"
#include "AprMd5.hxx"
#include "FileHeaders.hxx"
#include "file_address.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/Vary.hxx"
#include "istream/istream.hxx"
#include "istream/FileIstream.hxx"
#include "io/FileDescriptor.hxx"
#include "util/StringCompare.hxx"
#include "util/StringStrip.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"

#include <sodium.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

static void
DispatchUnauthorized(Request &request2) noexcept
{
	HttpHeaders headers;
	headers.Write("www-authenticate",
		      "Basic realm=\"Geschuetzter Bereich\"");
	request2.DispatchResponse(HTTP_STATUS_UNAUTHORIZED, std::move(headers),
				  "Unauthorized");
}

static char *
FindWhitespace(char *s) noexcept
{
	for (; *s != 0; ++s)
		if (IsWhitespaceFast(*s))
			return s;

	return nullptr;
}

static const char *
CheckUsername(const char *s, StringView user) noexcept
{
	const char *t = StringAfterPrefixIgnoreCase(s, user);
	if (t == nullptr || *t != ':')
		return nullptr;

	return t + 1;
}

static const char *
FindUserPassword(char *s, StringView user) noexcept
{
	while (true) {
		s = StripLeft(s);
		if (*s == 0 || StringStartsWith(s, "#~"))
			return nullptr;

		char *end = FindWhitespace(s);
		if (end == nullptr)
			return nullptr;

		*end = 0;

		const char *password_hash = CheckUsername(s, user);
		if (password_hash != nullptr)
			return password_hash;

		s = end + 1;
	}
}

static std::pair<std::string, std::string>
ParseBasicAuth(const char *authorization) noexcept
{
	const char *s = StringAfterPrefixIgnoreCase(authorization, "basic ");
	if (s == nullptr)
		return {};

	s = StripLeft(s);

	char buffer[1024];
	size_t length;
	const char *end;

	if (sodium_base642bin((unsigned char *)buffer, sizeof(buffer) - 1,
			      s, strlen(s),
			      nullptr, &length, &end,
			      sodium_base64_VARIANT_ORIGINAL) != 0)
		return {};

	s = StripLeft(end);
	if (*s != 0)
		return {};

	buffer[length] = 0;

	const char *colon = strchr(buffer, ':');
	if (colon == nullptr)
		return {};

	return std::make_pair(std::string(buffer, colon - buffer),
			      std::string(colon + 1));
}

static char *
ReadFirstLine(FileDescriptor fd, char *buffer, size_t size) noexcept
{
	ssize_t nbytes = pread(fd.Get(), buffer, size - 1, 0);
	if (nbytes <= 0)
		return nullptr;

	char *end = std::find(buffer, buffer + nbytes, '\n');
	*end = 0;

	return buffer;
}

static bool
VerifyPassword(const char *crypted_password,
	       const char *given_password) noexcept
{
	if (IsAprMd5(crypted_password)) {
		const auto result = AprMd5(given_password, crypted_password);
		return strcmp(crypted_password, result.c_str()) == 0;
	}

	char *p = crypt(given_password, crypted_password);
	if (p == nullptr)
		return false;

	return strcmp(p, crypted_password) == 0;
}

static FILE *
OpenSiblingFile(const char *path, const char *sibling_name)
{
	const char *slash = strrchr(path, '/');
	if (slash == nullptr)
		return nullptr;

	char buffer[4096];
	if (size_t(slash + 1 - path) + strlen(sibling_name) >= sizeof(buffer))
		return nullptr;

	strcpy((char *)mempcpy(buffer, path, slash + 1 - path), sibling_name);
	return fopen(buffer, "r");
}

static bool
CheckAccessFileFor(const StringMap &request_headers, const char *html_path)
{
	FILE *file = OpenSiblingFile(html_path, ".access");
	if (file == nullptr)
		return true;

	AtScopeExit(file) { fclose(file); };

	const char *authorization = request_headers.Get("authorization");
	if (authorization == nullptr)
		return false;

	const auto basic_auth = ParseBasicAuth(authorization);
	if (basic_auth.first.empty())
		return false;

	const StringView username = basic_auth.first.c_str();
	const auto given_password = basic_auth.second.c_str();

	char buffer[4096];
	while (fgets(buffer, sizeof(buffer), file) != nullptr) {
		char *line = Strip(buffer);
		const char *crypted_password = CheckUsername(line, username);
		if (crypted_password != nullptr)
			return VerifyPassword(crypted_password, given_password);
	}

	return false;
}

bool
Request::EmulateModAuthEasy(const FileAddress &address,
			    UniqueFileDescriptor &fd,
			    const struct statx &st) noexcept
{
	if (!CheckAccessFileFor(request.headers, address.path)) {
		DispatchUnauthorized(*this);
		return true;
	}

	if (!StringEndsWith(address.path, ".html"))
		return false;

	char buffer[4096];
	char *line = ReadFirstLine(fd, buffer, sizeof(buffer));
	if (line == nullptr)
		return false;

	char *s = StripLeft(line);
	s = const_cast<char *>(StringAfterPrefix(s, "<!--"));
	if (s == nullptr)
		return false;

	s = StripLeft(s);
	s = const_cast<char *>(StringAfterPrefix(s, "~#"));
	if (s == nullptr)
		return false;

	if (!IsWhitespaceNotNull(*s))
		return false;

	const char *authorization = request.headers.Get("authorization");
	if (authorization == nullptr) {
		DispatchUnauthorized(*this);
		return true;
	}

	const auto basic_auth = ParseBasicAuth(authorization);
	if (basic_auth.first.empty()) {
		DispatchUnauthorized(*this);
		return true;
	}

	const char *password =
		FindUserPassword(s, basic_auth.first.c_str());
	if (password == nullptr ||
	    !VerifyPassword(password, basic_auth.second.c_str())) {
		DispatchUnauthorized(*this);
		return true;
	}

	const TranslateResponse &tr = *translate.response;

	const char *override_content_type = translate.content_type;
	if (override_content_type == nullptr)
		override_content_type = address.content_type;

	HttpHeaders headers;
	GrowingBuffer &headers2 = headers.GetBuffer();
	file_response_headers(headers2,
			      instance.event_loop.GetSystemClockCache(),
			      override_content_type,
			      fd, st,
			      tr.expires_relative,
			      IsProcessorFirst());
	write_translation_vary_header(headers2, tr);

	http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;

	DispatchResponse(status, std::move(headers),
			 istream_file_fd_new(instance.event_loop, pool,
					     address.path,
					     std::move(fd), 0, st.stx_size));

	return true;
}
