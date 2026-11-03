// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "memory/ExpansibleBuffer.hxx"

#include <string_view>

#include <assert.h>
#include <sys/types.h>

struct pool;

enum class XmlParserTagType {
	OPEN,
	CLOSE,
	SHORT,

	/** XML processing instruction */
	PI,
};

struct XmlParserTag {
	off_t start, end;
	std::string_view name;
	XmlParserTagType type;
};

struct XmlParserAttribute {
	off_t name_start, value_start, value_end, end;
	std::string_view name, value;

	/**
	 * Was the value enclosed in quotes?  If not, then a
	 * substitution must add them, because the value may not
	 * contain whitespace.
	 */
	bool value_quoted;
};

class XmlParserHandler {
public:
	/**
	 * A tag has started, and we already know its name.
	 *
	 * @return true if attributes should be parsed, false otherwise
	 * (saves CPU cycles; OnXmlTagFinished() is not called)
	 */
	virtual bool OnXmlTagStart(const XmlParserTag &tag) noexcept = 0;

	/**
	 * @return false if the #XmlParser has been closed inside the
	 * method
	 */
	virtual bool OnXmlTagFinished(const XmlParserTag &tag) noexcept = 0;

	virtual void OnXmlAttributeFinished(const XmlParserAttribute &attr) noexcept = 0;
	virtual size_t OnXmlCdata(std::string_view text, bool escaped,
				  off_t start) noexcept = 0;
};

class XmlParser final {
	off_t position = 0;

	/* internal state */
	enum class State {
		NONE,

		/** within a SCRIPT element; only accept "</" to break out */
		SCRIPT,

		/** found '<' within a SCRIPT element */
		SCRIPT_ELEMENT_NAME,

		/** parsing an element name */
		ELEMENT_NAME,

		/** inside the element tag */
		ELEMENT_TAG,

		/** parsing attribute name */
		ATTR_NAME,

		/** after the attribute name, waiting for '=' */
		AFTER_ATTR_NAME,

		/** after the '=', waiting for the attribute value */
		BEFORE_ATTR_VALUE,

		/** parsing the quoted attribute value */
		ATTR_VALUE,

		/** compatibility with older and broken HTML: attribute value
		    without quotes */
		ATTR_VALUE_COMPAT,

		/** found a slash, waiting for the '>' */
		SHORT,

		/** inside the element, currently unused */
		INSIDE,

		/** parsing a declaration name beginning with "<!" */
		DECLARATION_NAME,

		/** within a CDATA section */
		CDATA_SECTION,

		/** within a comment */
		COMMENT,
	} state = State::NONE;

	/* element */
	XmlParserTag tag;
	char tag_name[64];
	size_t tag_name_length;

	/**
	 * Was #tag_name too long?  The tag is parsed as usual, but
	 * the handler never sees it.
	 */
	bool tag_name_overflow;

	/**
	 * Is the handler interested in the current tag?  If not, the
	 * handler is not invoked and no attribute value is collected.
	 */
	bool tag_interesting;

	/* attribute */
	char attr_name[64];
	size_t attr_name_length;

	/**
	 * Was #attr_name or #attr_value too long?  The attribute is
	 * parsed as usual, but it is not passed to the handler,
	 * because its name or value would be truncated.
	 */
	bool attr_overflow;
	char attr_value_delimiter;
	ExpansibleBuffer attr_value;
	XmlParserAttribute attr;

	/** in a CDATA section, how many characters have been matching
	    CDEnd ("]]>")? */
	size_t cdend_match;

	/** in a comment, how many consecutive minus are there? */
	unsigned minus_count;

	XmlParserHandler &handler;

public:
	XmlParser(struct pool &pool,
		  XmlParserHandler &_handler) noexcept;

	/**
	 * @return the number of bytes consumed or 0 if this object
	 * has been destroyed
	 */
	size_t Feed(const char *start, size_t length) noexcept;

	void Script() noexcept {
		assert(state == State::NONE ||
		       state == State::INSIDE);

		state = State::SCRIPT;
	}

private:
	/**
	 * Append to #attr_value, but only if the value is going to be
	 * used.
	 *
	 * @return false if the hard limit was exceeded
	 */
	void WriteAttributeValue(std::string_view src) noexcept {
		if (!attr_overflow && tag_interesting &&
		    !attr_value.Write(src))
			attr_overflow = true;
	}

	void InvokeAttributeFinished() noexcept;
};
