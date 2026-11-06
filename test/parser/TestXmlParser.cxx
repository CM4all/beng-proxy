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

/**
 * Like #RecordingXmlParserHandler, but only "c:" elements are
 * "interesting", just like #WidgetContainerParser does.  Records the
 * name of every element the parser finds.
 */
class BoringXmlParserHandler final : public XmlParserHandler {
public:
	std::vector<std::string> tags;
	std::vector<CdataCall> cdata;

	/* virtual methods from class XmlParserHandler */
	bool OnXmlTagStart(const XmlParserTag &tag) noexcept override {
		tags.emplace_back(tag.name);
		return tag.name.starts_with("c:"sv);
	}

	bool OnXmlTagFinished(const XmlParserTag &) noexcept override {
		return true;
	}

	void OnXmlAttributeFinished(const XmlParserAttribute &) noexcept override {
	}

	size_t OnXmlCdata(std::string_view text, bool,
			  off_t start) noexcept override {
		cdata.emplace_back(std::string{text}, start);
		return text.size();
	}
};

struct BoringInstance {
	RootPool root_pool;

	BoringXmlParserHandler handler;

	XmlParser parser{root_pool, handler};

	void Feed(std::string_view src) noexcept {
		parser.Feed(src.data(), src.size());
	}

	[[nodiscard]]
	std::string GetCdata() const noexcept {
		std::string result;
		for (const auto &i : handler.cdata)
			result += i.text;
		return result;
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

/**
 * A '>' inside a quoted attribute value of an uninteresting element
 * must not end the tag; else the rest of the value is parsed as
 * markup and the attacker can inject elements into the template.
 */
TEST(XmlParser, BoringTagQuotedGreaterThan)
{
	BoringInstance instance;

	instance.Feed("<input value=\"x><c:widget type='t' id='i'/>\">text"sv);

	const std::vector<std::string> expected_tags{"input"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * Like BoringTagQuotedGreaterThan, but the injected markup is a
 * comment, which would swallow the rest of the document.
 */
TEST(XmlParser, BoringTagQuotedComment)
{
	BoringInstance instance;

	instance.Feed("<div title=\"x><!--\">text"sv);

	const std::vector<std::string> expected_tags{"div"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * An unquoted attribute value still ends at '>'.
 */
TEST(XmlParser, BoringTagUnquotedGreaterThan)
{
	BoringInstance instance;

	instance.Feed("<input value=x><c:widget type='t' id='i'/>text"sv);

	const std::vector<std::string> expected_tags{"input", "c:widget"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * An attribute name which starts with a character that is not a XML
 * name start character (e.g. the Angular/Vue syntax) must not close
 * the tag; else the following attribute values are parsed as markup.
 */
TEST(XmlParser, AngularAttributeName)
{
	BoringInstance instance;

	instance.Feed("<a @click=\"f()\" title=\"<c:widget type='t' id='i'/>\">text"sv);

	const std::vector<std::string> expected_tags{"a"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * Like AngularAttributeName, with a bracket and a comment.
 */
TEST(XmlParser, BracketAttributeName)
{
	BoringInstance instance;

	instance.Feed("<img [src]=\"u\" alt=\"<!--\">text"sv);

	const std::vector<std::string> expected_tags{"img"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * A closing tag with a stray slash must not loop forever.
 */
TEST(XmlParser, StraySlashInClosingTag)
{
	BoringInstance instance;

	instance.Feed("</div/>text"sv);

	const std::vector<std::string> expected_tags{"div"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * An attribute value which is too long for the buffer must still be
 * consumed up to its closing delimiter; else its text is parsed as
 * markup.  The attribute itself is dropped, because its value would
 * be truncated.
 */
TEST(XmlParser, OverlongAttributeValue)
{
	BoringInstance instance;

	std::string input{"<a href=\""};
	input.append(9000, 'x');
	input += "<c:widget type='t' id='i'/>\">text";

	instance.Feed(input);

	const std::vector<std::string> expected_tags{"a"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * Like OverlongAttributeValue, but the element is "interesting", so
 * the attribute would be passed to the handler.
 */
TEST(XmlParser, OverlongAttributeValueInteresting)
{
	Instance instance;

	std::string input{"<c:widget href=\""};
	input.append(9000, 'x');
	input += "<c:widget type='t' id='i'/>\">text";

	instance.Feed(input);

	/* the over-long attribute was dropped ... */
	EXPECT_TRUE(instance.handler.attributes.empty());

	/* ... but the parser stayed inside the tag */
	ExpectMonotonic(instance.handler.cdata);
	ExpectWellFormed(instance.handler.attributes);

	std::string cdata;
	for (const auto &i : instance.handler.cdata)
		cdata += i.text;
	EXPECT_EQ(cdata, "text"sv);
}

/**
 * An over-long unquoted attribute value is consumed up to the end of
 * the tag, and the attribute is dropped instead of being reported
 * with a truncated value.
 */
TEST(XmlParser, OverlongUnquotedAttributeValue)
{
	Instance instance;

	std::string input{"<c:widget href="};
	input.append(9000, 'x');
	input += ">text";

	instance.Feed(input);

	EXPECT_TRUE(instance.handler.attributes.empty());

	std::string cdata;
	for (const auto &i : instance.handler.cdata)
		cdata += i.text;
	EXPECT_EQ(cdata, "text"sv);
}

/**
 * An over-long element name must not leave the tag either.
 */
TEST(XmlParser, OverlongElementName)
{
	BoringInstance instance;

	std::string input{"<"};
	input.append(100, 'x');
	input += " href=\"<c:widget type='t' id='i'/>\">text";

	instance.Feed(input);

	/* the element name was too long, so the handler never saw
	   the element */
	EXPECT_TRUE(instance.handler.tags.empty());
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * A '/' which is not followed by '>' must not end the tag; else the
 * following attribute value is parsed as markup.
 */
TEST(XmlParser, StraySlashInOpeningTag)
{
	BoringInstance instance;

	instance.Feed("<div / title=\"<c:widget type='t' id='i'/>\">text"sv);

	const std::vector<std::string> expected_tags{"div"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * Like StraySlashInOpeningTag, without whitespace after the slash.
 */
TEST(XmlParser, StraySlashBeforeAttribute)
{
	BoringInstance instance;

	instance.Feed("<input/ value=\"<c:widget type='t' id='i'/>\">text"sv);

	const std::vector<std::string> expected_tags{"input"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * The attribute after a stray '/' is reported with correct offsets,
 * and the tag is not "short".
 */
TEST(XmlParser, StraySlashAttributeOffsets)
{
	Instance instance;

	/*             0123456789012345678901234 */
	instance.Feed("<c:widget href=\"x\"/ c:base=\"w\">text"sv);

	ExpectWellFormed(instance.handler.attributes);

	const std::vector<AttributeCall> expected{
		{"href", "x", 10, 16, 17, 18},
		{"c:base", "w", 20, 28, 29, 30},
	};

	EXPECT_EQ(instance.handler.attributes, expected);
}

/**
 * A "/>" still ends the tag.
 */
TEST(XmlParser, ShortTag)
{
	BoringInstance instance;

	instance.Feed("<div/><c:widget type='t' id='i'/>text"sv);

	const std::vector<std::string> expected_tags{"div", "c:widget"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * A byte which is not in the ASCII name set must not end the tag;
 * else the following attribute value is parsed as markup.
 */
TEST(XmlParser, NonAsciiElementName)
{
	BoringInstance instance;

	instance.Feed("<my-\xc3\xa9l title=\"<c:widget type='t' id='i'/>\">text"sv);

	const std::vector<std::string> expected_tags{"my-\xc3\xa9l"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * Like NonAsciiElementName, with an at sign in the element name.
 */
TEST(XmlParser, AtSignInElementName)
{
	BoringInstance instance;

	instance.Feed("<a@b title=\"<!--\">text"sv);

	const std::vector<std::string> expected_tags{"a@b"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
	EXPECT_EQ(instance.GetCdata(), "text"sv);
}

/**
 * A '<' which is not followed by a name character is text, and a
 * declaration is still recognized.
 */
TEST(XmlParser, NoElementName)
{
	BoringInstance instance;

	instance.Feed("<%foo><!DOCTYPE html><c:widget type='t' id='i'/>text"sv);

	const std::vector<std::string> expected_tags{"c:widget"};
	EXPECT_EQ(instance.handler.tags, expected_tags);
}
