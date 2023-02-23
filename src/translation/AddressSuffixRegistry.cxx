// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AddressSuffixRegistry.hxx"
#include "SuffixRegistry.hxx"
#include "ResourceAddress.hxx"
#include "file/Address.hxx"
#include "nfs/Address.hxx"
#include "util/CharUtil.hxx"
#include "util/Compiler.h"
#include "AllocatorPtr.hxx"

#include <span>

#include <string.h>

[[gnu::pure]]
static const char *
get_suffix(const char *path) noexcept
{
	const char *slash = strrchr(path, '/');
	if (slash != nullptr)
		path = slash + 1;

	while (*path == '.')
		++path;

	const char *dot = strrchr(path, '.');
	if (dot == nullptr || dot[1] == 0)
		return nullptr;

	return dot + 1;
}

struct AddressSuffixInfo {
	const char *path;
	std::span<const std::byte> content_type_lookup;
};

[[gnu::pure]]
static AddressSuffixInfo
GetAddressSuffixInfo(const ResourceAddress &address) noexcept
{
	switch (address.type) {
	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::HTTP:
	case ResourceAddress::Type::LHTTP:
	case ResourceAddress::Type::PIPE:
	case ResourceAddress::Type::CGI:
	case ResourceAddress::Type::FASTCGI:
	case ResourceAddress::Type::WAS:
		return {};

	case ResourceAddress::Type::LOCAL:
		return {address.GetFile().path, address.GetFile().content_type_lookup};

	case ResourceAddress::Type::NFS:
		return {address.GetNfs().path, address.GetNfs().content_type_lookup};
	}

	gcc_unreachable();
}

bool
suffix_registry_lookup(AllocatorPtr alloc, TranslationService &service,
		       const ResourceAddress &address,
		       const StopwatchPtr &parent_stopwatch,
		       SuffixRegistryHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
{
	const auto info = GetAddressSuffixInfo(address);
	if (info.content_type_lookup.data() == nullptr)
		return false;

	const char *suffix = get_suffix(info.path);
	if (suffix == nullptr)
		return false;

	const size_t length = strlen(suffix);
	if (length > 5)
		return false;

	/* duplicate the suffix, convert to lower case, check for
	   "illegal" characters (non-alphanumeric) */
	char *buffer = alloc.Dup(suffix);
	for (char *p = buffer; *p != 0; ++p) {
		const char ch = *p;
		if (IsUpperAlphaASCII(ch))
			/* convert to lower case */
			*p += 'a' - 'A';
		else if (!IsLowerAlphaASCII(ch) && !IsDigitASCII(ch))
			/* no, we won't look this up */
			return false;
	}

	suffix_registry_lookup(alloc, service,
			       info.content_type_lookup, buffer,
			       parent_stopwatch,
			       handler, cancel_ptr);
	return true;
}
