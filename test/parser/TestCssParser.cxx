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
