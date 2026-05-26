// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/pcre/SharedRegex.hxx"

#include <string>

namespace Pcre { class Cache; }

/**
 * An item in a URI layout.
 *
 * @see TranslationCommand::LAYOUT
 */
struct TranslationLayoutItem {
	enum class Type {
		EXACT,
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
	Pcre::SharedRegex regex;

	Type type;

	/**
	 * @see TranslationCommand::ACCESS_CONTROL_ALLOW_ALL
	 */
	bool access_control_allow_all = false;

	TranslationLayoutItem() = default;

	[[nodiscard]]
	explicit constexpr TranslationLayoutItem(Type _type, std::string_view _value) noexcept
		:value(_value), type(_type) {}

	[[nodiscard]]
	explicit TranslationLayoutItem(Type _type, std::string_view _value,
				       Pcre::Cache &pcre_cache) noexcept;

	Type GetType() const noexcept {
		return type;
	}

	[[gnu::pure]]
	bool Match(const char *uri) const noexcept;
};
