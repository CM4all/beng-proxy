// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Instance.hxx"
#include "file/Address.hxx"
#include "ResourceAddress.hxx"

#include <fcntl.h> // for O_PATH, O_DIRECTORY

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

	(this->*handler.file.open_base_callback)(fd);
}

inline void
Request::OpenBase(std::string_view path,
		  Handler::File::OpenBaseCallback callback) noexcept
{
	handler.file.open_base_callback = callback;

	static constexpr struct open_how open_directory_path{
		.flags = O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC,
		.resolve = RESOLVE_NO_MAGICLINKS,
	};

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
	if (address.base != nullptr)
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
	case ResourceAddress::Type::NFS:
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
