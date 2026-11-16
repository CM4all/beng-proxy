// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "StartBengProxy.hxx"
#include "TranslationServerThread.hxx"
#include "translation/server/Connection.hxx"
#include "translation/server/Handler.hxx"
#include "translation/server/Request.hxx"
#include "translation/server/Response.hxx"
#include "lib/curl/Easy.hxx"
#include "lib/curl/StringGlue.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/SpanCast.hxx"
#include "util/StringStrip.hxx"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <stdlib.h> // for exit()
#include <unistd.h>

using std::string_view_literals::operator""sv;

namespace {

class TestTranslationHandler final : public Translation::Server::Handler {
	static constexpr std::string_view PL_REGEX = ".+\\.pl$"sv;

	const std::string document_root = fmt::format("{}/htdocs"sv, getenv("srcdir"));

public:
	bool OnTranslationRequest(Translation::Server::Connection &connection,
				  const Translation::Server::Request &request,
				  CancellablePointer &cancel_ptr) noexcept override;

private:
	void HandleRootLayoutBase(std::string_view base,
				  const Translation::Server::Request &request,
				  Translation::Server::Response &response) const noexcept;

	void HandleEnotdirLayout(const Translation::Server::Request &request,
				 Translation::Server::Response &response) const noexcept;

	void OnTranslationRequest(const Translation::Server::Request &request,
				  Translation::Server::Response &response) const noexcept;
};

inline void
TestTranslationHandler::HandleRootLayoutBase(const std::string_view base,
					     const Translation::Server::Request &request,
					     Translation::Server::Response &response) const noexcept
{
	if (base == "/simple/"sv) {
		response.Base(base).EasyBase().Path(document_root, "/"sv).DocumentRoot(document_root);
	} else if (base == "/index/"sv) {
		if (request.directory_index.data() != nullptr) {
			if (request.probe_path_suffixes.data() == nullptr) {
				response.Base(base).EasyBase().TestPath(document_root, "/"sv).ProbePathSuffixes(nullptr, {"index.txt"sv});
			} else if (request.probe_suffix != nullptr) {
				response.Base(base).EasyBase().Path(document_root, "/"sv).DocumentRoot(document_root).AppendPath(request.probe_suffix);
			} else {
				response.Base(base).Status(HttpStatus::NOT_FOUND);
			}

			return;
		}

		response.Base(base).EasyBase().Path(document_root, "/"sv).DocumentRoot(document_root).DirectoryIndex("dummy"sv).DirectoryIndexSlash();
	} else if (base == "/enotdir/"sv) {
		response.Base(base)
			.Layout("enotdir"sv)
			.Regex(PL_REGEX);
	} else
		std::terminate();
}

inline void
TestTranslationHandler::HandleEnotdirLayout(const Translation::Server::Request &request,
					    Translation::Server::Response &response) const noexcept
{
	constexpr std::string_view base = "/enotdir/"sv;

	if (request.regex != nullptr && PL_REGEX == request.regex) {
		response.Base(base).EasyBase().Regex(PL_REGEX).RegexTail()
			.CGI("/:"sv).ExpandPath(document_root, "/\\0"sv)
			.ExpandScriptName(base, "\\0"sv)
			.DocumentRoot(document_root);
	} else {
		response.Base(base).EasyBase()
			.Path(document_root, "/"sv)
			.DocumentRoot(document_root)
			.Enotdir("dummy"sv);
	}
}

inline void
TestTranslationHandler::OnTranslationRequest(const Translation::Server::Request &request,
					     Translation::Server::Response &response) const noexcept
{
	if (request.host == nullptr || request.uri == nullptr) {
		response.Status(HttpStatus::BAD_REQUEST);
		return;
	}

	if (const auto layout = ToStringView(request.layout);
	    layout.data() == nullptr) {
		response.Layout("root"sv)
			.Base("/simple/"sv)
			.Base("/index/"sv)
			.Base("/enotdir/"sv);
		return;
	} else if (layout == "root"sv) {
		if (request.base != nullptr) {
			HandleRootLayoutBase(request.base, request, response);
			return;
		}
	} else if (layout == "enotdir"sv) {
		HandleEnotdirLayout(request, response);
		return;
	}

	response.Base("/"sv).Status(HttpStatus::I_M_A_TEAPOT);
}

bool
TestTranslationHandler::OnTranslationRequest(Translation::Server::Connection &connection,
					     const Translation::Server::Request &request,
					     [[maybe_unused]] CancellablePointer &cancel_ptr) noexcept
{
	Translation::Server::Response response;
	OnTranslationRequest(request, response);
	return connection.SendResponse(std::move(response));
}

static std::string
ToString(const Curl::StringResponse &response) noexcept
{
	std::string s = fmt::format("{}"sv, std::to_underlying(response.status));

	if (auto i = response.headers.find("location"sv); i != response.headers.end()) {
		s.append(" location="sv);
		s.append(i->second);
	}

	if (!response.body.empty()) {
		s.push_back(' ');
		s.append(StripRight(response.body));
	}

	return s;
}

class BengProxyTest : public ::testing::Test {
	BengProxyProcess bp = StartBengProxy(fmt::format("{}/beng-proxy.conf"sv, getenv("srcdir")).c_str(),
					     "foo");

	TestTranslationHandler translation_handler;
	TranslationServerThread translation_server{
		std::move(bp.translation_listener_socket),
		translation_handler,
	};

protected:
	std::string StringRequest(const char *url) {
		return ToString(Curl::StringRequest(bp.CreateCurl(url)));
	}
};

} // anonymous namespace

TEST_F(BengProxyTest, All)
{
	// simple static files
	EXPECT_EQ(StringRequest("http://dummy/simple/"), "404 Not a regular file"sv);
	EXPECT_EQ(StringRequest("http://dummy/simple/index.txt"), "200 hello"sv);
	EXPECT_EQ(StringRequest("http://dummy/simple/../meson.build"), "400 Malformed URI"sv);

	// can't escape from the LAYOUT constraints
	EXPECT_EQ(StringRequest("http://dummy/../simple/"), "400 Malformed URI"sv);
	EXPECT_EQ(StringRequest("http://dummy/./simple/"), "400 Ambiguous URI"sv);
	// TODO EXPECT_EQ(StringRequest("http://dummy/simpl%65/"), "400 Ambiguous URI"sv);

	// static files with static index file
	EXPECT_EQ(StringRequest("http://dummy/index/index.txt"), "200 hello"sv);
	EXPECT_EQ(StringRequest("http://dummy/index/with_index/"), "200 hello2"sv);
	EXPECT_EQ(StringRequest("http://dummy/index/with_index"), "303 location=/index/with_index/ redirection"sv);
	EXPECT_EQ(StringRequest("http://dummy/index/with_index/../index.txt"), "400 Malformed URI"sv);
	EXPECT_EQ(StringRequest("http://dummy/index/"), "200 hello"sv);

	// CGI with PATH_INFO
	EXPECT_EQ(StringRequest("http://dummy/enotdir/index.txt"), "200 hello"sv);
	EXPECT_EQ(StringRequest("http://dummy/enotdir/dump.pl/foo?q"), "200 REQUEST_URI=/enotdir/dump.pl/foo?q SCRIPT_NAME=/enotdir/dump.pl PATH_INFO=/foo QUERY_STRING=q"sv);
	EXPECT_EQ(StringRequest("http://dummy/enotdir/dump.pl?q"), "200 REQUEST_URI=/enotdir/dump.pl?q SCRIPT_NAME=/enotdir/dump.pl PATH_INFO= QUERY_STRING=q"sv);

	// not allowed without UNSAFE_BASE
	EXPECT_EQ(StringRequest("http://dummy/enotdir/dump.pl/./foo?q"), "400 Malformed URI"sv);
	EXPECT_EQ(StringRequest("http://dummy/enotdir/dump.pl/../foo?q"), "400 Malformed URI"sv);

	// not mapped
	EXPECT_EQ(StringRequest("http://dummy/unknown"), "418"sv);
}
