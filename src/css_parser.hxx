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
class CssParser;

struct CssParserValue {
	off_t start, end;
	StringView value;
};

struct CssParserHandler {
	/**
	 * A class name was found.
	 */
	void (*class_name)(const CssParserValue *name, void *ctx);

	/**
	 * A XML id was found.
	 */
	void (*xml_id)(const CssParserValue *id, void *ctx);

	/**
	 * A new block begins.  Optional method.
	 */
	void (*block)(void *ctx);

	/**
	 * A property value with a keyword value.  Optional method.
	 */
	void (*property_keyword)(const char *name, StringView value,
				 off_t start, off_t end, void *ctx);

	/**
	 * A property value with a URL was found.  Optional method.
	 */
	void (*url)(const CssParserValue *url, void *ctx);

	/**
	 * The command "@import" was found.  Optional method.
	 */
	void (*import)(const CssParserValue *url, void *ctx);

	/**
	 * The CSS end-of-file was reached.
	 */
	void (*eof)(void *ctx, off_t length);

	/**
	 * An I/O error has occurred.
	 */
	void (*error)(std::exception_ptr ep, void *ctx);
};

/**
 * Simple parser for CSS (Cascading Style Sheets).
 *
 * @param block true when the input consists of only one block
 */
CssParser *
css_parser_new(struct pool &pool, UnusedIstreamPtr input, bool block,
	       const CssParserHandler &handler, void *handler_ctx);

/**
 * Force-closen the CSS parser, don't invoke any handler methods.
 */
void
css_parser_close(CssParser *parser);

/**
 * Ask the CSS parser to read and parse more CSS source code.  Does
 * nothing if the istream blocks.
 */
void
css_parser_read(CssParser *parser);
