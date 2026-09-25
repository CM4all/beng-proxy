// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "parser/XmlParser.hxx"
#include "pool/RootPool.hxx"

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <vector>

using std::string_view_literals::operator""sv;

namespace {

struct CdataCall {
	std::string text;
	off_t start;

	bool operator==(const CdataCall &) const noexcept = default;
};

std::ostream &
operator<<(std::ostream &os, const CdataCall &c)
{
	return os << "{\"" << c.text << "\", " << c.start << '}';
}

class RecordingXmlParserHandler final : public XmlParserHandler {
public:
	std::vector<CdataCall> cdata;

	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &) noexcept override {
		return false;
	}

	bool OnXmlTagFinished(const XmlParserTag &) noexcept override {
		return true;
	}

	void OnXmlAttributeFinished(const XmlParserAttribute &) noexcept override {}

	size_t OnXmlCdata(std::string_view text, bool,
			  off_t start) noexcept override {
		cdata.emplace_back(std::string{text}, start);
		return text.size();
	}
};

struct Instance {
	RootPool root_pool;

	RecordingXmlParserHandler handler;

	XmlParser parser{root_pool, handler};

	void Feed(std::string_view src) noexcept {
		parser.Feed(src.data(), src.size());
	}
};

/**
 * The text ranges reported by XmlParser::Feed() must be
 * non-overlapping and monotonically increasing; #XmlProcessor turns
 * each of them into a ReplaceIstream::Settle() call, which asserts
 * exactly that.
 */
void
ExpectMonotonic(const std::vector<CdataCall> &cdata) noexcept
{
	off_t end = 0;

	for (const auto &i : cdata) {
		EXPECT_GE(i.start, end);
		end = i.start + (off_t)i.text.size();
	}
}

} // anonymous namespace

/**
 * A "]]" which turns out not to be the start of "]]>" is reported
 * again; it used to be reported at the offset of the byte which
 * followed it, i.e. two bytes too late, so the following one-byte
 * text started before the end of the previous one.
 */
TEST(XmlParser, CdataRestoredBrackets)
{
	Instance instance;

	/*             0123456789 */
	instance.Feed("<![CDATA[]]c]]>"sv);

	ExpectMonotonic(instance.handler.cdata);

	const std::vector<CdataCall> expected{
		{"]]", 9},
		{"c", 11},
	};

	EXPECT_EQ(instance.handler.cdata, expected);
}

/**
 * Like CdataRestoredBrackets, but the byte following the restored
 * "]]" is another "]" and the CDATA section is unterminated.
 */
TEST(XmlParser, CdataRestoredBracketsAtEnd)
{
	Instance instance;

	instance.Feed("<![CDATA[]]]]"sv);

	ExpectMonotonic(instance.handler.cdata);

	const std::vector<CdataCall> expected{
		{"]]", 9},
		{"]", 11},
	};

	EXPECT_EQ(instance.handler.cdata, expected);
}

/**
 * The partial "]]" match spans two Feed() calls; the restored text
 * must still be reported at its real offset.
 */
TEST(XmlParser, CdataRestoredBracketsSplit)
{
	Instance instance;

	instance.Feed("<![CDATA[]]"sv);
	instance.Feed("c"sv);

	ExpectMonotonic(instance.handler.cdata);

	const std::vector<CdataCall> expected{
		{"]]", 9},
		{"c", 11},
	};

	EXPECT_EQ(instance.handler.cdata, expected);
}

/**
 * Inside a SCRIPT element, a '<' which does not start a closing tag
 * is reported as text; it used to be reported at the offset of the
 * following byte, so it overlapped the text after it.
 */
TEST(XmlParser, ScriptRestoredBracket)
{
	Instance instance;

	instance.parser.Script();

	/*             0123 */
	instance.Feed("ab<c"sv);

	ExpectMonotonic(instance.handler.cdata);

	const std::vector<CdataCall> expected{
		{"ab", 0},
		{"<", 2},
		{"c", 3},
	};

	EXPECT_EQ(instance.handler.cdata, expected);
}
