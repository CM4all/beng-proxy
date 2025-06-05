// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeChallenge.hxx"
#include "lib/fmt/RuntimeError.hxx"

static constexpr const char *acme_challenge_status_strings[] = {
	"pending",
	"processing",
	"valid",
	"invalid",
	nullptr
};

AcmeChallenge::Status
AcmeChallenge::ParseStatus(const std::string_view s)
{
	for (size_t i = 0; acme_challenge_status_strings[i] != nullptr; ++i)
		if (s == acme_challenge_status_strings[i])
			return Status(i);

	throw FmtRuntimeError("Invalid challenge status: {}", s);
}

const char *
AcmeChallenge::FormatStatus(Status s) noexcept
{
	return acme_challenge_status_strings[size_t(s)];
}

void
AcmeChallenge::Check() const
{
	if (error)
		std::rethrow_exception(error);

	switch (status) {
	case Status::PENDING:
	case Status::PROCESSING:
	case Status::VALID:
		break;

	case Status::INVALID:
		throw FmtRuntimeError("Challenge status is '{}'",
				      AcmeChallenge::FormatStatus(status));
	}
}
