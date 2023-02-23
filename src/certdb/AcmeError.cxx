// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeError.hxx"
#include "json/String.hxx"
#include "util/Exception.hxx"

#include <boost/json.hpp>

static std::string
MakeAcmeErrorMessage(const boost::json::object &error) noexcept
{
	const auto *detail = error.if_contains("detail");
	if (detail == nullptr)
		return "Server error";

	std::string msg = "Server error: ";
	msg.append(detail->as_string());
	return msg;
}

AcmeError::AcmeError(const boost::json::object &error)
	:std::runtime_error(MakeAcmeErrorMessage(error)),
	 type(Json::GetString(error.if_contains("type")))
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
