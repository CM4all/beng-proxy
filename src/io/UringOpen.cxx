// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UringOpen.hxx"
#include "io/FileAt.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/uring/Open.hxx"
#include "io/uring/Handler.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

#include <memory>

#include <fcntl.h>

class UringOpenOperation final : Cancellable, Uring::OpenHandler {
	std::unique_ptr<Uring::Open> open_stat;

	Uring::OpenHandler &handler;

public:
	UringOpenOperation(Uring::Queue &uring,
			   FileAt file, int flags,
			   Uring::OpenHandler &_handler,
			   CancellablePointer &cancel_ptr) noexcept
		:open_stat(new Uring::Open(uring, *this)),
		 handler(_handler)
	{
		cancel_ptr = *this;

		open_stat->StartOpen(file, flags);
	}

private:
	void Destroy() noexcept {
		this->~UringOpenOperation();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* keep the Uring::Open allocated until the kernel
		   finishes the operation, or else the kernel may
		   overwrite the memory when something else occupies
		   it; also, the canceled object will take care for
		   closing the new file descriptor */
		open_stat->Cancel();
		open_stat.release();

		Destroy();
	}

	/* virtual methods from class Uring::OpenHandler */
	void OnOpen(UniqueFileDescriptor fd) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnOpen(std::move(fd));
	}

	void OnOpenError(int error) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnOpenError(error);
	}
};

void
UringOpen(Uring::Queue &uring, AllocatorPtr alloc,
	  FileAt file, int flags,
	  Uring::OpenHandler &handler,
	  CancellablePointer &cancel_ptr) noexcept
{
	alloc.New<UringOpenOperation>(uring, file, flags,
				      handler, cancel_ptr);
}
