// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "cgi/Address.hxx"
#include "AllocatorPtr.hxx"
#include "TestPool.hxx"

#include <string_view>

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static constexpr auto
MakeCgiAddress(const char *executable_path, const char *uri,
	       const char *script_name, const char *path_info) noexcept
{
	CgiAddress address(executable_path);
	address.uri = uri;
	address.script_name = script_name;
	address.path_info = path_info;
	return address;
}

TEST(CgiAddressTest, Uri)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	CgiAddress a("/usr/bin/cgi");
	ASSERT_FALSE(a.IsExpandable());
	ASSERT_STREQ(a.GetURI(alloc), "/");

	a.script_name = "/";
	ASSERT_STREQ(a.GetURI(alloc), "/");

	a.path_info = "foo";
	ASSERT_STREQ(a.GetURI(alloc), "/foo");

	a.query_string = "";
	ASSERT_STREQ(a.GetURI(alloc), "/foo?");

	a.query_string = "a=b";
	ASSERT_STREQ(a.GetURI(alloc), "/foo?a=b");

	a.path_info = "";
	ASSERT_STREQ(a.GetURI(alloc), "/?a=b");

	a.path_info = nullptr;
	ASSERT_STREQ(a.GetURI(alloc), "/?a=b");

	a.script_name = "/test.cgi";
	a.path_info = nullptr;
	a.query_string = nullptr;
	ASSERT_STREQ(a.GetURI(alloc), "/test.cgi");

	a.path_info = "/foo";
	ASSERT_STREQ(a.GetURI(alloc), "/test.cgi/foo");

	a.script_name = "/bar/";
	ASSERT_STREQ(a.GetURI(alloc), "/bar/foo");

	a.script_name = "/";
	ASSERT_STREQ(a.GetURI(alloc), "/foo");

	a.script_name = nullptr;
	ASSERT_STREQ(a.GetURI(alloc), "/foo");
}

TEST(CgiAddressTest, Apply)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	auto a = MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/foo");

	auto b = a.Apply(alloc, "");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/foo");

	b = a.Apply(alloc, "bar");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/bar");

	a.path_info = "/foo/";
	ASSERT_EQ(true, a.IsValidBase());

	b = a.Apply(alloc, "bar");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/foo/bar");

	b = a.Apply(alloc, "/bar");
	ASSERT_NE(b, nullptr);
	ASSERT_NE(b, &a);
	ASSERT_FALSE(b->IsValidBase());
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->script_name, a.script_name);
	ASSERT_STREQ(b->path_info, "/bar");
}

TEST(CgiAddressTest, RelativeTo)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	static constexpr auto base = MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/foo/");

	ASSERT_EQ(MakeCgiAddress("/usr/bin/other-cgi", nullptr, "/test.pl", "/foo/").RelativeTo(base).data(), nullptr);

	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", nullptr).RelativeTo(base).data(), nullptr);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/").RelativeTo(base).data(), nullptr);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/foo").RelativeTo(base).data(), nullptr);
	ASSERT_NE(MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/foo/").RelativeTo(base).data(), nullptr);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/foo/").RelativeTo(base), ""sv);
	ASSERT_EQ(MakeCgiAddress("/usr/bin/cgi", nullptr, "/test.pl", "/foo/bar").RelativeTo(base), "bar"sv);
}

TEST(CgiAddressTest, AutoBase)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	static constexpr auto cgi0 =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, nullptr, "/");

	const char *ab = cgi0.AutoBase(alloc, "/");
	ASSERT_NE(ab, nullptr);
	ASSERT_STREQ(ab, "/");

	ASSERT_EQ(cgi0.AutoBase(alloc, "/foo"), nullptr);

	static constexpr auto cgi1 =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, nullptr, "foo/bar");

	ASSERT_EQ(cgi1.AutoBase(alloc, "/"), nullptr);
	ASSERT_EQ(cgi1.AutoBase(alloc, "/foo/bar"), nullptr);

	static constexpr auto cgi2 =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, nullptr, "/bar/baz");

	ASSERT_EQ(cgi2.AutoBase(alloc, "/"), nullptr);
	ASSERT_EQ(cgi2.AutoBase(alloc, "/foobar/baz"), nullptr);

	ab = cgi2.AutoBase(alloc, "/foo/bar/baz");
	ASSERT_NE(ab, nullptr);
	ASSERT_STREQ(ab, "/foo/");
}

TEST(CgiAddressTest, AutoBaseEmptyPathInfo)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	/* empty PATH_INFO */
	static constexpr auto cgi3 =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, "/script/", "");

	const char *ab = cgi3.AutoBase(alloc, "/");
	ASSERT_NE(ab, nullptr);
	ASSERT_STREQ(ab, "/");

	ab = cgi3.AutoBase(alloc, "/foo/");
	ASSERT_NE(ab, nullptr);
	ASSERT_STREQ(ab, "/foo/");
}

TEST(CgiAddressTest, AutoBaseScriptNameSlash)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	/* SCRIPT_NAME ends with slash */
	static constexpr auto cgi4 =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, "/script/", "abc");

	ASSERT_EQ(cgi4.AutoBase(alloc, "/"), nullptr);

	const char *ab = cgi4.AutoBase(alloc, "/foo/abc");
	ASSERT_NE(ab, nullptr);
	ASSERT_STREQ(ab, "/foo/");
}

TEST(CgiAddressTest, BaseNoPathInfo)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	static constexpr auto src =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl", nullptr, nullptr, nullptr);

	const auto *dest = src.SaveBase(alloc, "");
	ASSERT_NE(dest, nullptr);
	ASSERT_STREQ(dest->path, src.path);
	ASSERT_TRUE(dest->path_info == nullptr ||
		    strcmp(dest->path_info, "") == 0);

	dest = src.LoadBase(alloc, "foo/bar");
	ASSERT_NE(dest, nullptr);
	ASSERT_STREQ(dest->path, src.path);
	ASSERT_STREQ(dest->path_info, "foo/bar");
}

TEST(CgiAddressTest, SaveLoadBase)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	static constexpr auto src =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl",
			       "/foo/bar/baz",
			       nullptr,
			       "/bar/baz");

	const auto *a = src.SaveBase(alloc, "bar/baz");
	ASSERT_NE(a, nullptr);
	ASSERT_STREQ(a->path, src.path);
	ASSERT_STREQ(a->path_info, "/");

	const auto *b = a->LoadBase(alloc, "");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/");
	ASSERT_STREQ(b->path_info, "/");

	b = a->LoadBase(alloc, "xyz");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/xyz");
	ASSERT_STREQ(b->path_info, "/xyz");

	a = src.SaveBase(alloc, "baz");
	ASSERT_NE(a, nullptr);
	ASSERT_STREQ(a->path, src.path);
	ASSERT_STREQ(a->uri, "/foo/bar/");
	ASSERT_STREQ(a->path_info, "/bar/");

	b = a->LoadBase(alloc, "bar/");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/bar/bar/");
	ASSERT_STREQ(b->path_info, "/bar/bar/");

	b = a->LoadBase(alloc, "bar/xyz");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/bar/bar/xyz");
	ASSERT_STREQ(b->path_info, "/bar/bar/xyz");
}

TEST(CgiAddressTest, SaveLoadBaseScriptNameSlash)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	static constexpr auto src =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl",
			       "/foo/bar/baz",
			       "/foo/",
			       "bar/baz");

	const auto *a = src.SaveBase(alloc, "bar/baz");
	ASSERT_NE(a, nullptr);
	ASSERT_STREQ(a->uri, "/foo/");
	ASSERT_STREQ(a->script_name, "/foo/");
	ASSERT_STREQ(a->path, src.path);
	ASSERT_STREQ(a->path_info, "");

	const auto *b = a->LoadBase(alloc, "");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/");
	ASSERT_STREQ(b->script_name, "/foo/");
	ASSERT_STREQ(b->path_info, "");

	b = a->LoadBase(alloc, "xyz");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/xyz");
	ASSERT_STREQ(b->script_name, "/foo/");
	ASSERT_STREQ(b->path_info, "xyz");

	a = src.SaveBase(alloc, "baz");
	ASSERT_NE(a, nullptr);
	ASSERT_STREQ(a->path, src.path);
	ASSERT_STREQ(a->uri, "/foo/bar/");
	ASSERT_STREQ(a->script_name, "/foo/");
	ASSERT_STREQ(a->path_info, "bar/");

	b = a->LoadBase(alloc, "bar/");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/bar/bar/");
	ASSERT_STREQ(b->script_name, "/foo/");
	ASSERT_STREQ(b->path_info, "bar/bar/");

	b = a->LoadBase(alloc, "bar/xyz");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/bar/bar/xyz");
	ASSERT_STREQ(b->script_name, "/foo/");
	ASSERT_STREQ(b->path_info, "bar/bar/xyz");
}

TEST(CgiAddressTest, SaveLoadBaseEmptyPathInfo)
{
	TestPool pool;
	const AllocatorPtr alloc{pool};

	static constexpr auto src =
		MakeCgiAddress("/usr/lib/cgi-bin/foo.pl",
			       "/foo/",
			       "/foo/",
			       "");

	const auto *a = src.SaveBase(alloc, "");
	ASSERT_NE(a, nullptr);
	ASSERT_STREQ(a->uri, "/foo/");
	ASSERT_STREQ(a->script_name, "/foo/");
	ASSERT_STREQ(a->path, src.path);
	ASSERT_STREQ(a->path_info, "");

	const auto *b = a->LoadBase(alloc, "");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/");
	ASSERT_STREQ(b->script_name, "/foo/");
	ASSERT_STREQ(b->path_info, "");

	b = a->LoadBase(alloc, "xyz");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->path, src.path);
	ASSERT_STREQ(b->uri, "/foo/xyz");
	ASSERT_STREQ(b->script_name, "/foo/");
	ASSERT_STREQ(b->path_info, "xyz");
}
