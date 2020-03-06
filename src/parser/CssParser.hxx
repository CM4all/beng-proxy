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

#include "istream/Sink.hxx"
#include "util/StringView.hxx"
#include "util/TrivialArray.hxx"

#include <exception>

#include <sys/types.h>

struct pool;
class UnusedIstreamPtr;

struct CssParserValue {
	off_t start, end;
	StringView value;
};

struct CssParserHandler {
	/**
	 * A class name was found.
	 */
	void (*class_name)(const CssParserValue *name, void *ctx) noexcept;

	/**
	 * A XML id was found.
	 */
	void (*xml_id)(const CssParserValue *id, void *ctx) noexcept;

	/**
	 * A new block begins.  Optional method.
	 */
	void (*block)(void *ctx) noexcept;

	/**
	 * A property value with a keyword value.  Optional method.
	 */
	void (*property_keyword)(const char *name, StringView value,
				 off_t start, off_t end, void *ctx) noexcept;

	/**
	 * A property value with a URL was found.  Optional method.
	 */
	void (*url)(const CssParserValue *url, void *ctx) noexcept;

	/**
	 * The command "@import" was found.  Optional method.
	 */
	void (*import)(const CssParserValue *url, void *ctx) noexcept;

	/**
	 * The CSS end-of-file was reached.
	 */
	void (*eof)(void *ctx, off_t length) noexcept;

	/**
	 * An I/O error has occurred.
	 */
	void (*error)(std::exception_ptr ep, void *ctx) noexcept;
};

class CssParser final : IstreamSink, DestructAnchor {
	template<size_t max>
	class StringBuffer : public TrivialArray<char, max> {
	public:
		using TrivialArray<char, max>::capacity;
		using TrivialArray<char, max>::size;
		using TrivialArray<char, max>::raw;
		using TrivialArray<char, max>::end;

		size_t GetRemainingSpace() const noexcept {
			return capacity() - size();
		}

		void AppendTruncated(StringView p) noexcept {
			size_t n = std::min(p.size, GetRemainingSpace());
			std::copy_n(p.data, n, end());
			this->the_size += n;
		}

		constexpr operator StringView() const noexcept {
			return {raw(), size()};
		}

		gcc_pure
		bool Equals(StringView other) const noexcept {
			return other.Equals(*this);
		}

		template<size_t n>
		bool EqualsLiteral(const char (&value)[n]) const noexcept {
			return Equals({value, n - 1});
		}
	};

	const bool block;

	off_t position;

	const CssParserHandler &handler;
	void *const handler_ctx;

	/* internal state */
	enum class State {
		NONE,
		BLOCK,
		CLASS_NAME,
		XML_ID,
		DISCARD_QUOTED,
		PROPERTY,
		POST_PROPERTY,
		PRE_VALUE,
		VALUE,
		PRE_URL,
		URL,

		/**
		 * An '@' was found.  Feeding characters into "name".
		 */
		AT,
		PRE_IMPORT,
		IMPORT,
	} state;

	char quote;

	off_t name_start;
	StringBuffer<64> name_buffer;

	StringBuffer<64> value_buffer;

	off_t url_start;
	StringBuffer<1024> url_buffer;

public:
	CssParser(UnusedIstreamPtr input, bool block,
		  const CssParserHandler &handler, void *handler_ctx) noexcept;

	void Destroy() noexcept {
		this->~CssParser();
	}

	void Read() noexcept {
		input.Read();
	}

	void Close() noexcept {
		input.Close();
	}

private:
	size_t Feed(const char *start, size_t length) noexcept;

	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *data, size_t length) noexcept override {
		return Feed((const char *)data, length);
	}

	void OnEof() noexcept override {
		input.Clear();
		handler.eof(handler_ctx, position);
		Destroy();
	}

	void OnError(std::exception_ptr ep) noexcept override {
		input.Clear();
		handler.error(ep, handler_ctx);
		Destroy();
	}
};

/**
 * Simple parser for CSS (Cascading Style Sheets).
 *
 * @param block true when the input consists of only one block
 */
CssParser *
css_parser_new(struct pool &pool, UnusedIstreamPtr input, bool block,
	       const CssParserHandler &handler, void *handler_ctx) noexcept;

/**
 * Force-closen the CSS parser, don't invoke any handler methods.
 */
void
css_parser_close(CssParser *parser) noexcept;

/**
 * Ask the CSS parser to read and parse more CSS source code.  Does
 * nothing if the istream blocks.
 */
void
css_parser_read(CssParser *parser) noexcept;
