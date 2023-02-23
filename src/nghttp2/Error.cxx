// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Error.hxx"

#include <nghttp2/nghttp2.h>

namespace NgHttp2 {

ErrorCategory error_category;

std::string
ErrorCategory::message(int condition) const
{
	return nghttp2_strerror(condition);
}

} // namespace Avahi
