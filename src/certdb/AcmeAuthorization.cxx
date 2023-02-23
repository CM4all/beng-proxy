// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"
#include "lib/fmt/RuntimeError.hxx"

static constexpr const char *acme_authorization_status_strings[] = {
	"pending",
	"valid",
	"invalid",
	"deactivated",
	"expired",
	"revoked",
	nullptr
};

AcmeAuthorization::Status
AcmeAuthorization::ParseStatus(const std::string_view s)
{
	for (size_t i = 0; acme_authorization_status_strings[i] != nullptr; ++i)
		if (s == acme_authorization_status_strings[i])
			return Status(i);

	throw FmtRuntimeError("Invalid authorization status: {}", s);
}

const char *
AcmeAuthorization::FormatStatus(Status s) noexcept
{
	return acme_authorization_status_strings[size_t(s)];
}

const AcmeChallenge *
AcmeAuthorization::FindChallengeByType(const char *type) const noexcept
{
	for (const auto &i : challenges)
		if (i.type == type)
			return &i;

	return nullptr;
}
