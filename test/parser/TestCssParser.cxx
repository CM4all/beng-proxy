// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "parser/CssParser.hxx"

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <vector>

using std::string_view_literals::operator""sv;

namespace {

struct Property {
	std::string name, value;

	bool operator==(const Property &) const noexcept = default;
};

std::ostream &
operator<<(std::ostream &os, const Property &p)
{
	return os << "{\"" << p.name << "\", \"" << p.value << "\"}";
}

struct Recorder {
	std::vector<Property> properties;
	std::vector<std::string> urls;
	unsigned n_blocks = 0;

	static void ClassName(const CssParserValue *, void *) noexcept {}
	static void XmlId(const CssParserValue *, void *) noexcept {}

	static void Block(void *ctx) noexcept {
		++((Recorder *)ctx)->n_blocks;
	}

	static void PropertyKeyword(const char *name, std::string_view value,
				    off_t, off_t, void *ctx) noexcept {
		((Recorder *)ctx)->properties.emplace_back(name,
							   std::string{value});
	}

	static void Url(const CssParserValue *url, void *ctx) noexcept {
		((Recorder *)ctx)->urls.emplace_back(url->value);
	}

	static void Import(const CssParserValue *, void *) noexcept {}

	static constexpr CssParserHandler handler{
		ClassName, XmlId, Block, PropertyKeyword, Url, Import,
	};

	/**
	 * @param block true to parse a "style" attribute instead of a
	 * whole style sheet
	 */
	void Feed(bool block, std::string_view src) noexcept {
		CssParser parser{block, handler, this};
		EXPECT_EQ(parser.Feed(src.data(), src.size()), src.size());
	}
};

} // anonymous namespace

/**
 * A '}' in block mode (i.e. in a "style" attribute) does not end a
 * block because there is none; it used to be left unconsumed in
 * State::PRE_VALUE, making CssParser::Feed() spin forever.
 */
TEST(CssParser, BlockModeBraceAfterColon)
{
	Recorder recorder;
	recorder.Feed(true, "color:};background:red;"sv);

	const std::vector<Property> expected{
		{"color", ""},
		{"background", "red"},
	};

	EXPECT_EQ(recorder.properties, expected);
	EXPECT_TRUE(recorder.urls.empty());
}

/**
 * Like BlockModeBraceAfterColon, but the '}' appears in
 * State::PRE_URL.
 */
TEST(CssParser, BlockModeBraceAfterUrl)
{
	Recorder recorder;
	recorder.Feed(true, "background:url(};color:red;"sv);

	const std::vector<Property> expected{
		{"color", "red"},
	};

	EXPECT_EQ(recorder.properties, expected);
	EXPECT_TRUE(recorder.urls.empty());
}

/**
 * Outside of block mode, '}' still ends the block.
 */
TEST(CssParser, StyleSheet)
{
	Recorder recorder;
	recorder.Feed(false, "a{background:url(\"x.png\");color:red;}"sv);

	const std::vector<std::string> expected_urls{"x.png"};

	EXPECT_EQ(recorder.n_blocks, 1u);
	EXPECT_EQ(recorder.urls, expected_urls);
}

/**
 * A backslash escapes the following byte; an escaped quote must not
 * end the string, or else the rest of the attacker's string is parsed
 * as CSS and the URL rewrite breaks the string boundary.
 */
TEST(CssParser, EscapedQuoteInUrl)
{
	Recorder recorder;
	recorder.Feed(false, "a{background:url(\"x\\\");}.evil{color:red\");}"sv);

	const std::vector<std::string> expected_urls{"x\\\");}.evil{color:red"};

	EXPECT_EQ(recorder.urls, expected_urls);
	EXPECT_EQ(recorder.n_blocks, 1u);
}

/**
 * An escaped backslash does not escape the quote which follows it.
 */
TEST(CssParser, EscapedBackslashInUrl)
{
	Recorder recorder;
	recorder.Feed(false, "a{background:url(\"x\\\\\");color:red;}"sv);

	const std::vector<std::string> expected_urls{"x\\\\"};
	const std::vector<Property> expected_properties{{"color", "red"}};

	EXPECT_EQ(recorder.urls, expected_urls);
	EXPECT_EQ(recorder.properties, expected_properties);
}

/**
 * An escaped quote in a discarded string must not end it either.
 */
TEST(CssParser, EscapedQuoteInDiscardedString)
{
	Recorder recorder;
	recorder.Feed(false, "a{\"x\\\"}.evil{background:url(\\\"y\\\")\";color:red;}"sv);

	EXPECT_TRUE(recorder.urls.empty());

	const std::vector<Property> expected_properties{{"color", "red"}};
	EXPECT_EQ(recorder.properties, expected_properties);
}

/**
 * The escape state must survive a chunk boundary.
 */
TEST(CssParser, EscapeAcrossChunks)
{
	Recorder recorder;

	CssParser parser{false, Recorder::handler, &recorder};

	static constexpr auto a = "a{background:url(\"x\\"sv;
	static constexpr auto b = "\");}.evil{color:red\");}"sv;

	EXPECT_EQ(parser.Feed(a.data(), a.size()), a.size());
	EXPECT_EQ(parser.Feed(b.data(), b.size()), b.size());

	const std::vector<std::string> expected_urls{"x\\\");}.evil{color:red"};
	EXPECT_EQ(recorder.urls, expected_urls);
}
