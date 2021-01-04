/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "expansible_buffer.hxx"
#include "util/StringView.hxx"

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
	StringView name;
	XmlParserTagType type;
};

struct XmlParserAttribute {
	off_t name_start, value_start, value_end, end;
	StringView name, value;
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
	virtual size_t OnXmlCdata(StringView text, bool escaped,
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

		/** inside the element tag, but ignore attributes */
		ELEMENT_BORING,

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

	/* attribute */
	char attr_name[64];
	size_t attr_name_length;
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
	void InvokeAttributeFinished() noexcept;
};
