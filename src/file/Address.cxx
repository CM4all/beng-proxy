// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Address.hxx"
#include "delegate/Address.hxx"
#include "uri/Base.hxx"
#include "uri/Compare.hxx"
#include "uri/PEscape.hxx"
#include "pexpand.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <string.h>

FileAddress::FileAddress(AllocatorPtr alloc, const FileAddress &src,
			 const char *_path) noexcept
	:path(_path),
	 deflated(alloc.CheckDup(src.deflated)),
	 gzipped(alloc.CheckDup(src.gzipped)),
	 base(alloc.CheckDup(src.base)),
	 content_type(alloc.CheckDup(src.content_type)),
	 content_type_lookup(alloc.Dup(src.content_type_lookup)),
	 document_root(alloc.CheckDup(src.document_root)),
	 delegate(src.delegate != nullptr
		  ? alloc.New<DelegateAddress>(alloc, *src.delegate)
		  : nullptr),
	 auto_gzipped(src.auto_gzipped),
	 auto_brotli_path(src.auto_brotli_path),
	 expand_path(src.expand_path),
	 expand_document_root(src.expand_document_root)
{
}

FileAddress::FileAddress(AllocatorPtr alloc, const FileAddress &src) noexcept
	:FileAddress(alloc, src, alloc.Dup(src.path)) {}

void
FileAddress::Check() const
{
	if (delegate != nullptr)
		delegate->Check();
}

bool
FileAddress::IsValidBase() const noexcept
{
	return IsExpandable() ||
		(delegate == nullptr ? base != nullptr : is_base(path));
}

bool
FileAddress::SplitBase(AllocatorPtr alloc, const char *suffix) noexcept
{
	if (base != nullptr || delegate != nullptr || expand_path)
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
		return strcmp(path, ".") == 0
			? alloc.New<FileAddress>(alloc, *this)
			: nullptr;

	const char *end = UriFindUnescapedSuffix(path, suffix);
	if (end == nullptr)
		return nullptr;

	if (base != nullptr && end == path)
		return alloc.New<FileAddress>(alloc, *this, ".");

	const char *new_path = alloc.DupZ({path, end});
	const char *new_base = nullptr;

	if (delegate == nullptr) {
		new_base = new_path;
		new_path = ".";
	}

	auto *dest = alloc.New<FileAddress>(alloc, *this, new_path);
	dest->base = new_base;

	/* BASE+DEFLATED is not supported */
	dest->deflated = nullptr;
	dest->gzipped = nullptr;

	return dest;
}

FileAddress *
FileAddress::LoadBase(AllocatorPtr alloc, std::string_view suffix) const noexcept
{
	assert(path != nullptr);

	if (delegate != nullptr) {
		/* no "base" support for delegates */
		assert(*path != 0);
		assert(path[strlen(path) - 1] == '/');

		char *new_path = uri_unescape_concat(alloc, path, suffix);
		if (new_path == nullptr)
			return nullptr;

		return alloc.New<FileAddress>(alloc, *this, new_path);
	}

	const char *src_base = base;
	if (base == nullptr) {
		/* special case: this is an EASY_BASE call */
		assert(*path != 0);
		assert(path[strlen(path) - 1] == '/');

		src_base = path;
	} else {
		assert(strcmp(path, ".") == 0);
		assert(base != nullptr);
		assert(*base == '/');
		assert(base[strlen(base) - 1] == '/');
	}

	/* store the our path as "base" for the new instance */

	const char *new_path = uri_unescape_dup(alloc, suffix);
	if (new_path == nullptr)
		return nullptr;

	if (*new_path == 0)
		new_path = ".";
	else
		while (*new_path == '/')
			++new_path;

	auto *dest = alloc.New<FileAddress>(alloc, *this, new_path);
	dest->base = alloc.Dup(src_base);
	return dest;
}

bool
FileAddress::IsExpandable() const noexcept
{
	return expand_path ||
		expand_document_root ||
		(delegate != nullptr && delegate->IsExpandable());
}

void
FileAddress::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	if (expand_path) {
		expand_path = false;
		path = expand_string_unescaped(alloc, path, match_data);
	}

	if (expand_document_root) {
		expand_document_root = false;
		document_root = expand_string_unescaped(alloc, document_root,
							match_data);
	}

	if (delegate != nullptr)
		delegate->Expand(alloc, match_data);
}
