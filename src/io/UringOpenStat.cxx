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

		if (directory.IsDefined() && directory != FileDescriptor(AT_FDCWD))
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

	void OnOpenStatError(std::exception_ptr e) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnOpenStatError(std::move(e));
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
