// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Address.hxx"
#include "uri/Base.hxx"
#include "uri/Compare.hxx"
#include "uri/PEscape.hxx"
#include "util/StringAPI.hxx"
#include "pexpand.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>

FileAddress::FileAddress(AllocatorPtr alloc, const FileAddress &src,
			 const char *_path) noexcept
	:path(_path),
	 gzipped(alloc.CheckDup(src.gzipped)),
	 beneath(alloc.CheckDup(src.beneath)),
	 base(alloc.CheckDup(src.base)),
	 content_type(alloc.CheckDup(src.content_type)),
	 content_type_lookup(alloc.Dup(src.content_type_lookup)),
	 auto_gzipped(src.auto_gzipped),
	 auto_brotli_path(src.auto_brotli_path),
	 expand_path(src.expand_path)
{
}

FileAddress::FileAddress(AllocatorPtr alloc, const FileAddress &src) noexcept
	:FileAddress(alloc, src, alloc.Dup(src.path)) {}

void
FileAddress::Check() const
{
}

bool
FileAddress::IsValidBase() const noexcept
{
	return IsExpandable() || base != nullptr;
}

bool
FileAddress::SplitBase(AllocatorPtr alloc, const char *suffix) noexcept
{
	if (base != nullptr || expand_path)
		/* no-op and no error */
		return true;

	const char *end = UriFindUnescapedSuffix(path, suffix);
	if (end == nullptr)
		/* base mismatch */
		return false;

	base = alloc.DupZ({path, end});
	path = *end == 0 ? "." : end;
	return true;
}

FileAddress *
FileAddress::SaveBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	if (base != nullptr && suffix.empty())
		return StringIsEqual(path, ".")
			? alloc.New<FileAddress>(alloc, *this)
			: nullptr;

	const char *end = UriFindUnescapedSuffix(path, suffix);
	if (end == nullptr)
		return nullptr;

	if (base != nullptr && end == path)
		return alloc.New<FileAddress>(alloc, *this, ".");

	const char *new_path = ".";
	const char *new_base = alloc.DupZ({path, end});

	auto *dest = alloc.New<FileAddress>(alloc, *this, new_path);
	dest->base = new_base;

	/* BASE+GZIPPED is not supported */
	dest->gzipped = nullptr;

	return dest;
}

FileAddress *
FileAddress::LoadBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	assert(path != nullptr);

	const char *src_base = base;
	if (base == nullptr) {
		/* special case: this is an EASY_BASE call */
		assert(*path != 0);
		assert(path[strlen(path) - 1] == '/');

		src_base = path;
	} else {
		assert(StringIsEqual(path, "."));
		assert(base != nullptr);
		assert(*base == '/');
		assert(base[strlen(base) - 1] == '/');
	}

	/* store the our path as "base" for the new instance */

	const char *new_path = uri_unescape_dup(alloc, suffix);
	if (new_path == nullptr)
		return nullptr;

	while (*new_path == '/')
		++new_path;

	if (*new_path == 0)
		new_path = ".";

	auto *dest = alloc.New<FileAddress>(alloc, *this, new_path);
	dest->base = alloc.Dup(src_base);
	return dest;
}

bool
FileAddress::IsExpandable() const noexcept
{
	return expand_path;
}

void
FileAddress::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	if (expand_path) {
		expand_path = false;
		path = expand_string_unescaped(alloc, path, match_data);
	}
}
