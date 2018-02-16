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

#include "Lease.hxx"

FilteredSocketLease::~FilteredSocketLease() noexcept
{
    assert(IsReleased());

    socket->Destroy();
    delete socket;
}

void
FilteredSocketLease::Release(bool reuse) noexcept
{
    socket->Abandon();
    lease_ref.Release(reuse);
}

bool
FilteredSocketLease::IsEmpty() const noexcept
{
    return socket->IsEmpty();
}

size_t
FilteredSocketLease::GetAvailable() const noexcept
{
    return socket->GetAvailable();
}

WritableBuffer<void>
FilteredSocketLease::ReadBuffer() const noexcept
{
    return socket->ReadBuffer();
}

void
FilteredSocketLease::Consumed(size_t nbytes) noexcept
{
    socket->Consumed(nbytes);
}

bool
FilteredSocketLease::Read(bool expect_more) noexcept
{
    return socket->Read(expect_more);
}
