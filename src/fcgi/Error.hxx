// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <stdexcept>

/**
 * Error codes for #FcgiClientError.
 */
enum class FcgiClientErrorCode {
	UNSPECIFIED,

	/**
	 * ECONNREFUSED on the FastCGI listener.
	 */
	REFUSED,

	/**
	 * A socket I/O error has occurred.
	 */
	IO,

	/**
	 * Non-FastCGI garbage was received.
	 */
	GARBAGE,
};

class FcgiClientError : public std::runtime_error {
	FcgiClientErrorCode code;

public:
	FcgiClientError(FcgiClientErrorCode _code, const char *_msg)
		:std::runtime_error(_msg), code(_code) {}

	FcgiClientErrorCode GetCode() const {
		return code;
	}
};
