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
	QuoteOptional(os, address.expand_document_root
		      ? "expand_document_root" : "document_root",
		      address.document_root);

	if (address.delegate != nullptr)
		os << " delegate";
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
