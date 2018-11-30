/*
 * Copyright 2007-2018 Content Management AG
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

#include "PipeLease.hxx"
#include "pipe_stock.hxx"
#include "stock/Item.hxx"
#include "stock/Stock.hxx"
#include "system/Error.hxx"

#include <assert.h>

void
PipeLease::Release(bool reuse) noexcept
{
    if (!IsDefined())
        return;

    if (stock != nullptr) {
        assert(item != nullptr);
        item->Put(!reuse);
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
PipeLease::Create(struct pool &pool)
{
    assert(!IsDefined());

    if (stock != nullptr) {
        assert(item == nullptr);

        item = stock->GetNow(pool, nullptr);

        FileDescriptor fds[2];
        pipe_stock_item_get(item, fds);
        read_fd = fds[0];
        write_fd = fds[1];
    } else {
        if (!FileDescriptor::CreatePipeNonBlock(read_fd, write_fd))
            throw MakeErrno("pipe() failed");
    }
}
