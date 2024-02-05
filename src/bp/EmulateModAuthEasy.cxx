// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Instance.hxx"
#include "AprMd5.hxx"
#include "FileHeaders.hxx"
#include "file/Address.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/Vary.hxx"
#include "istream/FileIstream.hxx"
#include "io/FileDescriptor.hxx"
#include "util/StringCompare.hxx"
#include "util/StringStrip.hxx"
#include "util/CharUtil.hxx"
#include "util/ScopeExit.hxx"

#include <sodium.h>

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

static void
DispatchUnauthorized(Request &request2) noexcept
{
	HttpHeaders headers;
	headers.Write("www-authenticate",
		      "Basic realm=\"Geschuetzter Bereich\"");
	request2.DispatchError(HttpStatus::UNAUTHORIZED, std::move(headers),
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
CheckUsername(const char *s, std::string_view user) noexcept
{
	const char *t = StringAfterPrefixIgnoreCase(s, user);
	if (t == nullptr || *t != ':')
		return nullptr;

	return t + 1;
}

static const char *
FindUserPassword(char *s, std::string_view user) noexcept
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
	ssize_t nbytes = fd.ReadAt(0, buffer, size - 1);
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
OpenSiblingFile(FileDescriptor directory, std::string_view base_relative,
		const char *path,
		const char *sibling_name)
{
	char buffer[4096];

	const char *slash = strrchr(path, '/');
	if (slash != nullptr || !base_relative.empty()) {
		const std::string_view parent = slash != nullptr
			? std::string_view{path, slash + 1}
			: std::string_view{};
		const std::string_view sibling_name_v{sibling_name};

		if (base_relative.size() + parent.size() + sibling_name_v.size() >= sizeof(buffer))
			return nullptr;

		char *i = buffer;
		i = std::copy(base_relative.begin(), base_relative.end(), i);
		i = std::copy(parent.begin(), parent.end(), i);
		i = std::copy(sibling_name_v.begin(), sibling_name_v.end(), i);
		*i = 0;

		sibling_name = buffer;
	}

	FileDescriptor fd;
	if (!fd.Open(directory, sibling_name, O_RDONLY))
		return nullptr;

	return fdopen(fd.Get(), "r");
}

static bool
CheckAccessFileFor(FileDescriptor directory, std::string_view base_relative,
		   const StringMap &request_headers, const char *html_path)
{
	FILE *file = OpenSiblingFile(directory, base_relative,
				     html_path, ".access");
	if (file == nullptr)
		return true;

	AtScopeExit(file) { fclose(file); };

	const char *authorization = request_headers.Get("authorization");
	if (authorization == nullptr)
		return false;

	const auto basic_auth = ParseBasicAuth(authorization);
	if (basic_auth.first.empty())
		return false;

	const std::string_view username = basic_auth.first;
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
	if (!CheckAccessFileFor(handler.file.base, handler.file.base_relative,
				request.headers,
				address.path)) {
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
			      tr.GetExpiresRelative(HasQueryString()),
			      IsProcessorFirst(),
			      instance.config.use_xattr);
	write_translation_vary_header(headers2, tr);

	auto status = tr.status == HttpStatus{} ? HttpStatus::OK : tr.status;

	DispatchResponse(status, std::move(headers),
			 istream_file_fd_new(instance.event_loop, pool,
					     address.path,
					     std::move(fd), 0, st.stx_size));

	return true;
}
