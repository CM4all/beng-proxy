// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeError.hxx"
#include "lib/nlohmann_json/String.hxx"
#include "util/Exception.hxx"

using std::string_view_literals::operator""sv;

static std::string
MakeAcmeErrorMessage(const json &error) noexcept
{
	const auto detail = error.find("detail"sv);
	if (detail == error.end())
		return "Server error";

	std::string msg = "Server error: ";
	msg.append(detail->get<std::string_view>());
	return msg;
}

AcmeError::AcmeError(const json &error)
	:std::runtime_error(MakeAcmeErrorMessage(error)),
	 type(Json::GetStringRobust(error, "type"sv))
{
}

bool
IsAcmeErrorType(std::exception_ptr ep, const char *type) noexcept
{
	const auto *e = FindNested<AcmeError>(ep);
	return e != nullptr && e->GetType() == type;
}

bool
IsAcmeUnauthorizedError(std::exception_ptr ep) noexcept
{
	return IsAcmeErrorType(ep, "urn:acme:error:unauthorized");
}
