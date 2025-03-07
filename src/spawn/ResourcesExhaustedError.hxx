// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/Status.hxx"
#include "HttpMessageResponse.hxx"

/**
 * Exception class which indicatges that a child process has exhausted
 * its resource limits.
 */
class SpawnResourcesExhaustedError final : public HttpMessageResponse {
public:
	SpawnResourcesExhaustedError() noexcept
		:HttpMessageResponse(HttpStatus::SERVICE_UNAVAILABLE, "Resource limits exceeded") {}
};

