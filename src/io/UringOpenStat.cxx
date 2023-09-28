// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringOpenStat.hxx"
#include "io/FileAt.hxx"
#include "io/uring/OpenStat.hxx"
#include "io/uring/Handler.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

#include <memory>

#include <fcntl.h>

class UringOpenStatOperation final : Cancellable, Uring::OpenStatHandler {
	std::unique_ptr<Uring::OpenStat> open_stat;

	const UringOpenStatSuccessCallback on_success;
	const UringOpenStatErrorCallback on_error;

public:
	UringOpenStatOperation(Uring::Queue &uring, FileAt file,
			       UringOpenStatSuccessCallback _on_success,
			       UringOpenStatErrorCallback _on_error,
			       CancellablePointer &cancel_ptr) noexcept
		:open_stat(new Uring::OpenStat(uring, *this)),
		 on_success(_on_success), on_error(_on_error)
	{
		cancel_ptr = *this;

		if (file.directory.IsDefined())
			open_stat->StartOpenStatReadOnlyBeneath(file);
		else
			open_stat->StartOpenStatReadOnly(file);
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
		const auto _on_succes = on_success;

		/* delay destruction, because this object owns the
		   memory pointed to by "st" */
		const auto operation = std::move(open_stat);

		Destroy();

		_on_succes(std::move(fd), st);
	}

	void OnOpenStatError(int error) noexcept override {
		const auto _on_error = on_error;
		Destroy();
		_on_error(error);
	}
};

void
UringOpenStat(Uring::Queue &uring, AllocatorPtr alloc, FileAt file,
	      UringOpenStatSuccessCallback on_success,
	      UringOpenStatErrorCallback on_error,
	      CancellablePointer &cancel_ptr) noexcept
{
	alloc.New<UringOpenStatOperation>(uring, file,
					  on_success, on_error, cancel_ptr);
}
