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

#include "MemoryIstream.hxx"
#include "Bucket.hxx"

#include <algorithm>

off_t
MemoryIstream::_Skip(off_t length) noexcept
{
	size_t nbytes = std::min(off_t(data.size), length);
	data.skip_front(nbytes);
	Consumed(nbytes);
	return nbytes;
}

void
MemoryIstream::_Read() noexcept
{
	if (!data.empty()) {
		auto nbytes = InvokeData(data.data, data.size);
		if (nbytes == 0)
			return;

		data.skip_front(nbytes);
	}

	if (data.empty())
		DestroyEof();
}

void
MemoryIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	if (!data.empty())
		list.Push(data.ToVoid());
}

size_t
MemoryIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	if (nbytes > data.size)
		nbytes = data.size;
	data.skip_front(nbytes);
	return Consumed(nbytes);
}
