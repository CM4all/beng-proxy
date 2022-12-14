/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "http/Status.hxx"

#include <array>
#include <cstdint>
#include <cstddef>

constexpr std::array valid_http_status_array{
	HttpStatus{},

	HttpStatus::CONTINUE,
	HttpStatus::SWITCHING_PROTOCOLS,
	HttpStatus::OK,
	HttpStatus::CREATED,
	HttpStatus::ACCEPTED,
	HttpStatus::NON_AUTHORITATIVE_INFORMATION,
	HttpStatus::NO_CONTENT,
	HttpStatus::RESET_CONTENT,
	HttpStatus::PARTIAL_CONTENT,
	HttpStatus::MULTI_STATUS,
	HttpStatus::MULTIPLE_CHOICES,
	HttpStatus::MOVED_PERMANENTLY,
	HttpStatus::FOUND,
	HttpStatus::SEE_OTHER,
	HttpStatus::NOT_MODIFIED,
	HttpStatus::USE_PROXY,
	HttpStatus::TEMPORARY_REDIRECT,
	HttpStatus::BAD_REQUEST,
	HttpStatus::UNAUTHORIZED,
	HttpStatus::PAYMENT_REQUIRED,
	HttpStatus::FORBIDDEN,
	HttpStatus::NOT_FOUND,
	HttpStatus::METHOD_NOT_ALLOWED,
	HttpStatus::NOT_ACCEPTABLE,
	HttpStatus::PROXY_AUTHENTICATION_REQUIRED,
	HttpStatus::REQUEST_TIMEOUT,
	HttpStatus::CONFLICT,
	HttpStatus::GONE,
	HttpStatus::LENGTH_REQUIRED,
	HttpStatus::PRECONDITION_FAILED,
	HttpStatus::REQUEST_ENTITY_TOO_LARGE,
	HttpStatus::REQUEST_URI_TOO_LONG,
	HttpStatus::UNSUPPORTED_MEDIA_TYPE,
	HttpStatus::REQUESTED_RANGE_NOT_SATISFIABLE,
	HttpStatus::EXPECTATION_FAILED,
	HttpStatus::I_M_A_TEAPOT,
	HttpStatus::UNPROCESSABLE_ENTITY,
	HttpStatus::LOCKED,
	HttpStatus::FAILED_DEPENDENCY,
	HttpStatus::UPGRADE_REQUIRED,
	HttpStatus::PRECONDITION_REQUIRED,
	HttpStatus::TOO_MANY_REQUESTS,
	HttpStatus::REQUEST_HEADER_FIELDS_TOO_LARGE,
	HttpStatus::UNAVAILABLE_FOR_LEGAL_REASONS,
	HttpStatus::INTERNAL_SERVER_ERROR,
	HttpStatus::NOT_IMPLEMENTED,
	HttpStatus::BAD_GATEWAY,
	HttpStatus::SERVICE_UNAVAILABLE,
	HttpStatus::GATEWAY_TIMEOUT,
	HttpStatus::HTTP_VERSION_NOT_SUPPORTED,
	HttpStatus::INSUFFICIENT_STORAGE,
	HttpStatus::NETWORK_AUTHENTICATION_REQUIRED,
};

constexpr auto
GenerateHttpStatusToIndex() noexcept
{
	std::array<uint_least8_t, 550> result{};

	std::size_t i = 0;
	for (const auto &status : valid_http_status_array) {
		result[static_cast<std::size_t>(status)] = i++;
	}

	return result;
}

constexpr auto http_status_to_index = GenerateHttpStatusToIndex();

constexpr std::size_t
HttpStatusToIndex(HttpStatus status) noexcept
{
	std::size_t i = static_cast<std::size_t>(status);
	return i < http_status_to_index.size()
		? http_status_to_index[i]
		: 0U;
}

constexpr HttpStatus
IndexToHttpStatus(std::size_t i) noexcept
{
	return valid_http_status_array[i];
}
