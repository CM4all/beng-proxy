// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Lease.hxx"
#include "Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"

#include <assert.h>

void
PipeLease::Release(PutAction action) noexcept
{
	if (!IsDefined())
		return;

	if (stock != nullptr) {
		assert(item != nullptr);
		item->Put(action);
		item = nullptr;

		read_fd.SetUndefined();
		write_fd.SetUndefined();
	} else {
		if (read_fd.IsDefined())
			read_fd.Close();
		if (write_fd.IsDefined())
			write_fd.Close();
	}
}

void
PipeLease::Create()
{
	assert(!IsDefined());

	if (stock != nullptr) {
		assert(item == nullptr);

		item = stock->GetNow(nullptr);

		FileDescriptor fds[2];
		pipe_stock_item_get(item, fds);
		read_fd = fds[0];
		write_fd = fds[1];
	} else {
		if (!FileDescriptor::CreatePipeNonBlock(read_fd, write_fd))
			throw MakeErrno("pipe() failed");
	}
}
