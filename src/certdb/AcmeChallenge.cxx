/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "AcmeChallenge.hxx"
#include "util/RuntimeError.hxx"

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

	throw FormatRuntimeError("Invalid challenge status: %.*s",
				 int(s.size()), s.data());
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
		throw FormatRuntimeError("Challenge status is '%s'",
					 AcmeChallenge::FormatStatus(status));
	}
}
