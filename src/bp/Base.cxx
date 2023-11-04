// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Instance.hxx"
#include "file/Address.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"
#include "ResourceAddress.hxx"

#include <fcntl.h> // for O_PATH, O_DIRECTORY
#include <linux/openat2.h> // for RESOLVE_*

static constexpr struct open_how open_directory_path{
	.flags = O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC,
	.resolve = RESOLVE_NO_MAGICLINKS,
};

[[gnu::pure]]
static std::string_view
NormalizePath(std::string_view path) noexcept
{
	/* strip trailing slashes */
	while (path.ends_with('/'))
		path.remove_suffix(1);

	return path;
}

inline void
Request::OnBaseOpen(FileDescriptor fd, SharedLease lease) noexcept
{
	handler.file.base = fd;
	handler.file.base_lease = std::move(lease);
	handler.file.base_relative = {};

	(this->*handler.file.open_base_callback)(fd);
}

inline void
Request::OnBeneathOpen(FileDescriptor fd, SharedLease lease) noexcept
{
	const auto &address = *handler.file.address;
	assert(address.beneath != nullptr);

	handler.file.base = fd;
	handler.file.base_path = address.beneath;
	handler.file.base_relative = {};
	handler.file.beneath_lease = std::move(lease);

	if (address.base != nullptr) {
		/* check the relative path of BASE within BENEATH;
		   this prepares for inserting this prefix into paths
		   passed to StripBase() */
		std::string_view base{address.base};

		if (base.size() > handler.file.base_path.size() &&
		    base.starts_with(handler.file.base_path) &&
		    base[handler.file.base_path.size()] == '/')
			base = base.substr(handler.file.base_path.size() + 1);

		handler.file.base_relative = base;
	}

	(this->*handler.file.open_base_callback)(fd);
}

inline void
Request::OpenBeneath(const FileAddress &address,
		     Handler::File::OpenBaseCallback callback) noexcept
{
	assert(address.beneath != nullptr);

	handler.file.open_base_callback = callback;
	handler.file.address = &address;

	instance.fd_cache.Get(FileDescriptor::Undefined(), {}, address.beneath,
			      open_directory_path,
			      BIND_THIS_METHOD(OnBeneathOpen),
			      BIND_THIS_METHOD(OnBaseOpenError),
			      cancel_ptr);
}

inline void
Request::OpenBase(std::string_view path,
		  Handler::File::OpenBaseCallback callback) noexcept
{
	handler.file.open_base_callback = callback;

	instance.fd_cache.Get(FileDescriptor::Undefined(), {},
			      NormalizePath(path),
			      open_directory_path,
			      BIND_THIS_METHOD(OnBaseOpen),
			      BIND_THIS_METHOD(OnBaseOpenError),
			      cancel_ptr);
}

void
Request::OpenBase(const FileAddress &address,
		  Handler::File::OpenBaseCallback callback) noexcept
{
	handler.file.base_path = {};

	if (address.beneath != nullptr)
		OpenBeneath(address, callback);
	else if (address.base != nullptr)
		OpenBase(address.base, callback);
	else
		(this->*callback)(FileDescriptor::Undefined());
}

inline void
Request::OpenBase(const ResourceAddress &address,
		  Handler::File::OpenBaseCallback callback) noexcept
{
	switch (address.type) {
	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::HTTP:
	case ResourceAddress::Type::PIPE:
	case ResourceAddress::Type::CGI:
	case ResourceAddress::Type::FASTCGI:
	case ResourceAddress::Type::WAS:
	case ResourceAddress::Type::LHTTP:
		(this->*callback)(FileDescriptor::Undefined());
		break;

	case ResourceAddress::Type::LOCAL:
		OpenBase(address.GetFile(), callback);
		break;
	}
}

void
Request::OpenBase(const TranslateResponse &response,
		  Handler::File::OpenBaseCallback callback) noexcept
{
	OpenBase(response.address, callback);
}

const char *
Request::StripBase(const char *path) const noexcept
{
	if (handler.file.base_path.empty())
		return path;

	assert(handler.file.base_path.front() == '/');
	assert(handler.file.base_path.back() != '/');

	if (*path != '/' && !handler.file.base_relative.empty()) {
		/* this is a path relative to BASE, but we need it to
		   be relative to BENEATH, so insert "base_relative"
		   here */
		assert(handler.file.base_relative.back() == '/');

		const AllocatorPtr alloc{pool};
		return alloc.Concat(handler.file.base_relative, path);
	}

	const char *relative = StringAfterPrefix(path, handler.file.base_path);
	if (relative == nullptr)
		return path;

	if (*relative == 0)
		return ".";

	if (*relative != '/')
		return path;

	path = relative + 1;
	if (*path == 0)
		return ".";

	return path;
}
