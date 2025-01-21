// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "translation/Response.hxx"

#include <ostream>

inline std::ostream &
QuoteString(std::ostream &os, const char *s)
{
	return os << "\"" << s << "\"";
}

inline std::ostream &
QuoteAttribute(std::ostream &os, const char *name, const char *value)
{
	return QuoteString(os << " " << name << "=", value);
}

inline std::ostream &
QuoteOptional(std::ostream &os, const char *name, const char *value)
{
	return value != nullptr
		? QuoteAttribute(os, name, value)
		: os;
}

inline std::ostream &
operator<<(std::ostream &os, const FileAddress &address)
{
	QuoteString(os, address.path);

	if (address.expand_path)
		os << " expand_path";

	QuoteOptional(os, "base", address.base);

	// TODO: print more attributes
	return os;
}

inline std::ostream &
operator<<(std::ostream &os, const CgiAddress &address)
{
	if (address.path != nullptr)
		QuoteString(os, address.path);

	QuoteOptional(os, "interpreter", address.interpreter);
	QuoteOptional(os, "action", address.action);
	QuoteOptional(os, "uri", address.uri);
	QuoteOptional(os, "script_name", address.script_name);
	QuoteOptional(os, "path_info", address.path_info);
	QuoteOptional(os, "query_string", address.query_string);
	QuoteOptional(os, "document_root", address.document_root);

	// TODO: print more attributes
	return os;
}

inline std::ostream &
operator<<(std::ostream &os, const ResourceAddress &address)
{
	switch (address.type) {
	case ResourceAddress::Type::NONE:
		return os << "ResourceAddress::NONE";

	case ResourceAddress::Type::LOCAL:
		return os << "FileAddress{" << address.GetFile() << "}";

	case ResourceAddress::Type::CGI:
		return os << "CGI{" << address.GetCgi() << "}";

	case ResourceAddress::Type::FASTCGI:
		return os << "FastCGI{" << address.GetCgi() << "}";

	case ResourceAddress::Type::WAS:
		return os << "WAS{" << address.GetCgi() << "}";

	case ResourceAddress::Type::PIPE:
		return os << "Pipe{" << address.GetCgi() << "}";

	default:
		// TODO: implement
		return os << "ResourceAddress::???";
	}

	// unreachable
	return os;
}

inline std::ostream &
operator<<(std::ostream &os, const TranslateResponse &response)
{
	if (unsigned(response.status) != 0)
		os << " status=" << unsigned(response.status);

	if (response.address.IsDefined())
		os << " " << response.address;

	if (response.base != nullptr) {
		const char *name = "base";
		if (response.easy_base)
			name = "easy_base";

		QuoteAttribute(os, name, response.base);
	}

	QuoteOptional(os, "regex", response.regex);
	QuoteOptional(os, "inverse_regex", response.inverse_regex);

	return os;
}
