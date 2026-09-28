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

struct AttributeCall {
	std::string name, value;
	off_t name_start, value_start, value_end, end;

	bool operator==(const AttributeCall &) const noexcept = default;
};

std::ostream &
operator<<(std::ostream &os, const AttributeCall &a)
{
	return os << "{\"" << a.name << "\", \"" << a.value << "\", "
		  << a.name_start << ", " << a.value_start << ", "
		  << a.value_end << ", " << a.end << '}';
}

class RecordingXmlParserHandler final : public XmlParserHandler {
public:
	std::vector<CdataCall> cdata;
	std::vector<AttributeCall> attributes;

	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &) noexcept override {
		/* parse attributes */
		return true;
	}

	bool OnXmlTagFinished(const XmlParserTag &) noexcept override {
		return true;
	}

	void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept override {
		attributes.emplace_back(std::string{attr.name},
					std::string{attr.value},
					attr.name_start, attr.value_start,
					attr.value_end, attr.end);
	}

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

/**
 * Every reported attribute must satisfy
 * name_start <= value_start <= value_end <= end; #XmlProcessor feeds
 * (name_start, end) to ReplaceIstream::Add() to delete a c:base /
 * c:mode / xmlns:c attribute, which asserts start <= end.
 */
void
ExpectWellFormed(const std::vector<AttributeCall> &attributes) noexcept
{
	for (const auto &i : attributes) {
		EXPECT_LE(i.name_start, i.value_start);
		EXPECT_LE(i.value_start, i.value_end);
		EXPECT_LE(i.value_end, i.end);
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

/**
 * An attribute without a value used to be reported with the `end`
 * offset of the previously parsed attribute, which is before its own
 * `name_start`.
 */
TEST(XmlParser, AttributeWithoutValue)
{
	Instance instance;

	/*             0123456789 */
	instance.Feed("<a href=\"x\" c:base>"sv);

	ExpectWellFormed(instance.handler.attributes);

	const std::vector<AttributeCall> expected{
		{"href", "x", 3, 9, 10, 11},
		{"c:base", "", 12, 18, 18, 18},
	};

	EXPECT_EQ(instance.handler.attributes, expected);
}

/**
 * Like AttributeWithoutValue, but there are two of them; the first
 * one ends where the second one begins.
 */
TEST(XmlParser, TwoAttributesWithoutValue)
{
	Instance instance;

	instance.Feed("<a href=\"x\" c:base c:mode>"sv);

	ExpectWellFormed(instance.handler.attributes);

	const std::vector<AttributeCall> expected{
		{"href", "x", 3, 9, 10, 11},
		{"c:base", "", 12, 19, 19, 19},
		{"c:mode", "", 19, 25, 25, 25},
	};

	EXPECT_EQ(instance.handler.attributes, expected);
}

/**
 * A value without quotes.
 */
TEST(XmlParser, UnquotedAttributeValue)
{
	Instance instance;

	instance.Feed("<a href=x>"sv);

	ExpectWellFormed(instance.handler.attributes);

	const std::vector<AttributeCall> expected{
		{"href", "x", 3, 8, 9, 9},
	};

	EXPECT_EQ(instance.handler.attributes, expected);
}
