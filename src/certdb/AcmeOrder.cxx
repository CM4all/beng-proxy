// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeOrder.hxx"
#include "lib/fmt/RuntimeError.hxx"

static constexpr const char *acme_order_status_strings[] = {
	"pending",
	"ready",
	"processing",
	"valid",
	"invalid",
	nullptr
};

AcmeOrder::Status
AcmeOrder::ParseStatus(const std::string_view s)
{
	for (size_t i = 0; acme_order_status_strings[i] != nullptr; ++i)
		if (s == acme_order_status_strings[i])
			return Status(i);

	throw FmtRuntimeError("Invalid order status: {}", s);
}

const char *
AcmeOrder::FormatStatus(Status s) noexcept
{
	return acme_order_status_strings[size_t(s)];
}
