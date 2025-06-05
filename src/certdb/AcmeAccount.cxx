// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeAccount.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "util/StringCompare.hxx"

const char *
AcmeAccount::GetEmail() const noexcept
{
	for (const auto &i : contact) {
		const char *email = StringAfterPrefix(i.c_str(), "mailto:");
		if (email != nullptr)
			return email;
	}

	return nullptr;
}

static constexpr const char *acme_account_status_strings[] = {
	"valid",
	"deactivated",
	"revoked",
	nullptr
};

AcmeAccount::Status
AcmeAccount::ParseStatus(const std::string_view s)
{
	for (size_t i = 0; acme_account_status_strings[i] != nullptr; ++i)
		if (s == acme_account_status_strings[i])
			return Status(i);

	throw FmtRuntimeError("Invalid account status: {}", s);
}

const char *
AcmeAccount::FormatStatus(Status s) noexcept
{
	return acme_account_status_strings[size_t(s)];
}
