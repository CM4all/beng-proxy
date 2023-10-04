// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringStat.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "io/FileAt.hxx"
#include "util/Cancellable.hxx"

#include <sys/stat.h>

class UringStatOperation final : Cancellable, Uring::Operation {
	const UringStatSuccessCallback on_success;
	const UringStatErrorCallback on_error;

	struct statx st;

	bool canceled = false;

public:
	UringStatOperation(UringStatSuccessCallback _on_success,
			   UringStatErrorCallback _on_error) noexcept
		:on_success(_on_success), on_error(_on_error)
	{
	}

	void Start(Uring::Queue &queue, FileAt file,
		   int flags, unsigned mask,
		   CancellablePointer &cancel_ptr) noexcept {
		cancel_ptr = *this;

		auto &s = queue.RequireSubmitEntry();
		io_uring_prep_statx(&s, file.directory.Get(), file.name,
				    flags, mask, &st);
		queue.Push(s, *this);
	}

private:
	void Destroy() noexcept {
		delete this;
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		canceled = true;
	}

	/* virtual methods from class Uring::Operation */
	void OnUringCompletion(int res) noexcept override {
		if (res < 0) {
			auto _on_error = on_error;
			Destroy();
			_on_error(-res);
		} else {
			on_success(st);
			Destroy();
		}
	}
};

void
UringStat(Uring::Queue &queue, FileAt file, int flags, unsigned mask,
	  UringStatSuccessCallback on_success,
	  UringStatErrorCallback on_error,
	  CancellablePointer &cancel_ptr) noexcept
{
	auto *o = new UringStatOperation(on_success, on_error);
	o->Start(queue, file, flags, mask, cancel_ptr);
}
