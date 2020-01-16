/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "util/StringView.hxx"

#include <exception>

#include <sys/types.h>

struct pool;
class UnusedIstreamPtr;

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
	virtual size_t OnXmlCdata(const char *p, size_t length, bool escaped,
				  off_t start) noexcept = 0;
	virtual void OnXmlEof(off_t length) noexcept = 0;
	virtual void OnXmlError(std::exception_ptr ep) noexcept = 0;
};

class XmlParser;

XmlParser *
parser_new(struct pool &pool, UnusedIstreamPtr input,
	   XmlParserHandler &handler) noexcept;

/**
 * Close the parser object.  This function will not invoke
 * XmlParserHandler::OnXmlEof() and XmlParserHandler::OnXmlError().
 */
void
parser_close(XmlParser *parser) noexcept;

/**
 * @return false if the #XmlParser has been closed
 */
bool
parser_read(XmlParser *parser) noexcept;

void
parser_script(XmlParser *parser) noexcept;
