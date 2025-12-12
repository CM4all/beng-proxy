// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/pcre/UniqueRegex.hxx"

#include <string>

/**
 * An item in a URI layout.
 *
 * @see TranslationCommand::LAYOUT
 */
struct TranslationLayoutItem {
	enum class Type {
		BASE,
		REGEX,
	};

	/**
	 * The raw string as received from the translation server.
	 */
	std::string value;

	/**
	 * If #value is from a REGEX packet, then this field contains
	 * the compiled regex.
	 */
	UniqueRegex regex;

	Type type;

	TranslationLayoutItem() = default;

	struct Base {};
	TranslationLayoutItem(Base, std::string_view _value) noexcept
		:value(_value), type(Type::BASE) {}

	struct Regex {};
	TranslationLayoutItem(Regex, std::string_view _value)
		:value(_value), regex(value, {.anchored=true}), type(Type::REGEX) {}

	Type GetType() const noexcept {
		return type;
	}

	[[gnu::pure]]
	bool Match(const char *uri) const noexcept;
};
