// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <stdexcept>

class Widget;

enum class WidgetErrorCode {
	UNSPECIFIED,

	/**
	 * The content-type of the server's response does not meet our
	 * expectations.
	 */
	WRONG_TYPE,

	/**
	 * The response body is encoded in an unsupported way.
	 */
	UNSUPPORTED_ENCODING,

	/**
	 * The requested view does not exist.
	 */
	NO_SUCH_VIEW,

	/**
	 * Looking for a child widget inside a widget that is not a
	 * container.
	 */
	NOT_A_CONTAINER,

	/**
	 * The client request is forbidden due to formal reasons.
	 */
	FORBIDDEN,
};

class WidgetError : public std::runtime_error {
	WidgetErrorCode code;

public:
	WidgetError(WidgetErrorCode _code, const char *_msg)
		:std::runtime_error(_msg), code(_code) {}

	WidgetError(const Widget &widget, WidgetErrorCode _code, const char *_msg);

	WidgetErrorCode GetCode() const {
		return code;
	}
};
