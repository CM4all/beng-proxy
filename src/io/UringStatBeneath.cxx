// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UringStat.hxx"
#include "io/FileAt.hxx"
#include "io/uring/OpenStat.hxx"
#include "io/uring/Handler.hxx"
#include "util/Cancellable.hxx"

#include <memory>

class UringStatBeneathOperation final : Cancellable, Uring::OpenStatHandler {
	std::unique_ptr<Uring::OpenStat> open_stat;

	const UringStatSuccessCallback on_success;
	const UringStatErrorCallback on_error;

public:
	UringStatBeneathOperation(Uring::Queue &queue,
				  UringStatSuccessCallback _on_success,
				  UringStatErrorCallback _on_error) noexcept
		:open_stat(new Uring::OpenStat(queue, *this)),
		 on_success(_on_success), on_error(_on_error)
	{
	}

	void Start(FileAt file,
		   CancellablePointer &cancel_ptr) noexcept {
		assert(file.directory.IsDefined());

		cancel_ptr = *this;
		open_stat->StartStatBeneath(file);
	}

private:
	void Destroy() noexcept {
		delete this;
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
		fd.Close();

		_on_succes(st);
	}

	void OnOpenStatError(int error) noexcept override {
		const auto _on_error = on_error;
		Destroy();
		_on_error(error);
	}
};

void
UringStatBeneath(Uring::Queue &queue, FileAt file,
		 UringStatSuccessCallback on_success,
		 UringStatErrorCallback on_error,
		 CancellablePointer &cancel_ptr) noexcept
{
	auto *o = new UringStatBeneathOperation(queue, on_success, on_error);
	o->Start(file, cancel_ptr);
}
