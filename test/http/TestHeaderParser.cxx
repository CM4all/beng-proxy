// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "http/HeaderParser.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/fb_pool.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

#include <string>

#include <stdio.h> // for snprintf()

#include <unistd.h> // for alarm()

using std::string_view_literals::operator""sv;

namespace {

struct Instance {
	[[no_unique_address]]
	const ScopeFbPoolInit fb_pool_init;

	RootPool root_pool;

	/**
	 * Parse the given raw header block.
	 */
	StringMap Parse(std::string_view src) {
		GrowingBuffer gb;
		gb.Write(src);

		StringMap headers;
		header_parse_buffer(AllocatorPtr{root_pool}, headers,
				    std::move(gb));
		return headers;
	}
};

} // anonymous namespace

TEST(HeaderParser, Basic)
{
	Instance instance;

	const auto headers = instance.Parse("foo: 1\r\nbar: 2\r\n"sv);

	EXPECT_STREQ(headers.Get("foo"), "1");
	EXPECT_STREQ(headers.Get("bar"), "2");
}

/**
 * The last line is not terminated by a newline; it must still be
 * parsed, and the parser must not read past the end of the data.
 */
TEST(HeaderParser, UnterminatedLastLine)
{
	Instance instance;

	const auto headers = instance.Parse("foo: 1\nbar: 2"sv);

	EXPECT_STREQ(headers.Get("foo"), "1");
	EXPECT_STREQ(headers.Get("bar"), "2");
}

/**
 * Like UnterminatedLastLine, but the last line ends with a carriage
 * return (and no newline), which is what a truncated CRLF header
 * block looks like.
 */
TEST(HeaderParser, UnterminatedLastLineCr)
{
	Instance instance;

	const auto headers = instance.Parse("foo: 1\r\nbar: 2\r"sv);

	EXPECT_STREQ(headers.Get("foo"), "1");
	EXPECT_STREQ(headers.Get("bar"), "2");
}

/**
 * A header block much larger than the parser's staging buffer; the
 * buffer boundaries fall in the middle of header lines, and no line
 * may be lost there.
 */
TEST(HeaderParser, ManyLines)
{
	Instance instance;

	static constexpr std::size_t N = 1500;

	std::string src;
	for (std::size_t i = 0; i < N; ++i) {
		char line[64];
		snprintf(line, sizeof(line), "h%zu: v%zu\r\n", i, i);
		src += line;
	}

	/* several times the 4 kB staging buffer */
	ASSERT_GT(src.size(), 4096u * 3);

	const auto headers = instance.Parse(src);

	for (std::size_t i = 0; i < N; ++i) {
		char name[64], value[64];
		snprintf(name, sizeof(name), "h%zu", i);
		snprintf(value, sizeof(value), "v%zu", i);

		EXPECT_STREQ(headers.Get(name), value);
	}
}

/**
 * A header line which does not fit into the parser's staging buffer
 * used to make header_parse_buffer() spin forever, because it could
 * neither parse nor discard anything.
 */
TEST(HeaderParser, LongLine)
{
	Instance instance;

	std::string src{"foo: 1\n"};
	src += "long: ";
	src.append(8192, 'x');
	src += "\nbar: 2\n";

	/* don't hang the test suite if the bug comes back */
	alarm(30);

	const auto headers = instance.Parse(src);

	alarm(0);

	/* everything before the over-long line has been parsed ... */
	EXPECT_STREQ(headers.Get("foo"), "1");

	/* ... and the parser gave up at the over-long line */
	EXPECT_EQ(headers.Get("long"), nullptr);
	EXPECT_EQ(headers.Get("bar"), nullptr);
}

/**
 * Like LongLine, but the over-long line is the very first one.
 */
TEST(HeaderParser, LongFirstLine)
{
	Instance instance;

	std::string src{"long: "};
	src.append(8192, 'x');
	src += "\nfoo: 1\n";

	alarm(30);

	const auto headers = instance.Parse(src);

	alarm(0);

	EXPECT_EQ(headers.Get("long"), nullptr);
	EXPECT_EQ(headers.Get("foo"), nullptr);
}

/*
 * header_parse_find()
 *
 */

TEST(HeaderParseFind, NotFound)
{
	EXPECT_EQ(header_parse_find(""sv, "foo"sv).data(), nullptr);
	EXPECT_EQ(header_parse_find("bar: 1\n"sv, "foo"sv).data(), nullptr);
}

TEST(HeaderParseFind, Simple)
{
	EXPECT_EQ(header_parse_find("foo: 1\n"sv, "foo"sv), "1"sv);
}

TEST(HeaderParseFind, SecondLine)
{
	EXPECT_EQ(header_parse_find("a: 1\nfoo: 2\nb: 3\n"sv, "foo"sv), "2"sv);
}

/**
 * If the same header occurs twice, the first one is returned.
 */
TEST(HeaderParseFind, Duplicate)
{
	EXPECT_EQ(header_parse_find("foo: 1\nfoo: 2\n"sv, "foo"sv), "1"sv);
}

/**
 * A header whose name merely starts with the searched name is not a
 * match.
 */
TEST(HeaderParseFind, NameIsPrefix)
{
	EXPECT_EQ(header_parse_find("foobar: 1\n"sv, "foo"sv).data(), nullptr);
}

TEST(HeaderParseFind, Whitespace)
{
	EXPECT_EQ(header_parse_find("foo:1\n"sv, "foo"sv), "1"sv);
	EXPECT_EQ(header_parse_find("foo:    1\n"sv, "foo"sv), "1"sv);
	EXPECT_EQ(header_parse_find("foo: 1\r\n"sv, "foo"sv), "1"sv);

	/* whitespace between the name and the colon is tolerated */
	EXPECT_EQ(header_parse_find("foo : 1\n"sv, "foo"sv), "1"sv);
}

/**
 * An empty value is found (and is not the same as "not found").
 */
TEST(HeaderParseFind, EmptyValue)
{
	const auto value = header_parse_find("foo:\n"sv, "foo"sv);
	EXPECT_NE(value.data(), nullptr);
	EXPECT_TRUE(value.empty());
}

TEST(HeaderParseFind, UnterminatedLastLine)
{
	EXPECT_EQ(header_parse_find("a: 1\nfoo: 2"sv, "foo"sv), "2"sv);
}

/**
 * The name comparison is case-sensitive; all in-tree callers pass
 * lower-case names and header_write() is fed lower-case names.
 */
TEST(HeaderParseFind, CaseSensitive)
{
	EXPECT_EQ(header_parse_find("Foo: 1\n"sv, "foo"sv).data(), nullptr);
}

/**
 * This is the format actually produced by header_write(), i.e. what
 * HttpHeaders::GetSloppy() passes in.
 */
TEST(HeaderParseFind, Crlf)
{
	EXPECT_EQ(header_parse_find("a: 1\r\nfoo: 2\r\n"sv, "foo"sv), "2"sv);
}
