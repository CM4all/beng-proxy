// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/DestructObserver.hxx"
#include "util/TrivialArray.hxx"

#include <string_view>

#include <sys/types.h>

struct CssParserValue {
	off_t start, end;
	std::string_view value;
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
	void (*property_keyword)(const char *name, std::string_view value,
				 off_t start, off_t end, void *ctx) noexcept;

	/**
	 * A property value with a URL was found.  Optional method.
	 */
	void (*url)(const CssParserValue *url, void *ctx) noexcept;

	/**
	 * The command "@import" was found.  Optional method.
	 */
	void (*import)(const CssParserValue *url, void *ctx) noexcept;
};

/**
 * Simple parser for CSS (Cascading Style Sheets).
 */
class CssParser final : DestructAnchor {
	template<size_t max>
	class StringBuffer : public TrivialArray<char, max> {
	public:
		using TrivialArray<char, max>::capacity;
		using TrivialArray<char, max>::size;
		using TrivialArray<char, max>::data;
		using TrivialArray<char, max>::end;

		size_t GetRemainingSpace() const noexcept {
			return capacity() - size();
		}

		void AppendTruncated(std::string_view p) noexcept {
			size_t n = std::min(p.size(), GetRemainingSpace());
			std::copy_n(p.data(), n, end());
			this->the_size += n;
		}

		constexpr operator std::string_view() const noexcept {
			return {data(), size()};
		}

		[[gnu::pure]]
		bool Equals(std::string_view other) const noexcept {
			return other == *this;
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
	/**
	 * @param block true when the input consists of only one block
	 */
	CssParser(bool block,
		  const CssParserHandler &handler, void *handler_ctx) noexcept;

	/**
	 * Ask the CSS parser to read and parse more CSS source code.
	 *
	 * @return the number of bytes consumed or 0 if this object
	 * has been destroyed
	 */
	size_t Feed(const char *start, size_t length) noexcept;
};
