// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringOpenStat.hxx"
#include "io/uring/OpenStat.hxx"
#include "io/uring/Handler.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

#include <memory>

#include <fcntl.h>

class UringOpenStatOperation final : Cancellable, Uring::OpenStatHandler {
	std::unique_ptr<Uring::OpenStat> open_stat;

	Uring::OpenStatHandler &handler;

public:
	UringOpenStatOperation(Uring::Queue &uring,
			       FileDescriptor directory,
			       const char *path,
			       Uring::OpenStatHandler &_handler,
			       CancellablePointer &cancel_ptr) noexcept
		:open_stat(new Uring::OpenStat(uring, *this)),
		 handler(_handler)
	{
		cancel_ptr = *this;

		if (directory != FileDescriptor(AT_FDCWD))
			open_stat->StartOpenStatReadOnlyBeneath(directory, path);
		else
			open_stat->StartOpenStatReadOnly(directory, path);
	}

private:
	void Destroy() noexcept {
		this->~UringOpenStatOperation();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* keep the Uring::OpenStat allocated until the kernel
		   finishes the operation, or else the kernel may
		   overwrite the memory when something else occupies
		   it; also, the canceled object will take care for
		   closing the new file descriptor */
		open_stat->Cancel();
		open_stat.release();

		Destroy();
	}

	/* virtual methods from class Uring::OpenStatHandler */
	void OnOpenStat(UniqueFileDescriptor fd,
			struct statx &st) noexcept override {
		auto &_handler = handler;

		/* delay destruction, because this object owns the
		   memory pointed to by "st" */
		const auto operation = std::move(open_stat);

		Destroy();
		_handler.OnOpenStat(std::move(fd), st);
	}

	void OnOpenStatError(int error) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnOpenStatError(error);
	}
};

void
UringOpenStat(Uring::Queue &uring, AllocatorPtr alloc,
	      FileDescriptor directory,
	      const char *path,
	      Uring::OpenStatHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	alloc.New<UringOpenStatOperation>(uring, directory, path,
					  handler, cancel_ptr);
}
