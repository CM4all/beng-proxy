// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Client.hxx"
#include "net/log/Serializer.hxx"
#include "net/MsgHdr.hxx"
#include "net/SocketError.hxx"
#include "io/Iovec.hxx"

#include <errno.h>
#include <string.h> // for strerror()
#include <sys/socket.h> // for sendmmsg()

using namespace Net::Log;

inline bool
LogClient::Append(const Datagram &d) noexcept
try {
	assert(buffer_fill < buffer.size());
	assert(buffer_fill + max_size <= buffer.size());
	assert(n_vecs < vecs.size());

	auto w = std::span{buffer}.subspan(buffer_fill);
	assert(w.size() >= max_size);
	w = w.first(max_size);

	const auto size = Serialize(w, d);
	assert(size > 0);

	buffer_fill += size;
	vecs[n_vecs++] = MakeIovec(w.first(size));

	return true;
} catch (Net::Log::BufferTooSmall) {
	return false;
}

inline bool
LogClient::AppendRetry(const Net::Log::Datagram &d) noexcept
{
	if (n_vecs < vecs.size() && buffer_fill + max_size < buffer.size())
		return Append(d);

	/* not enough space in the buffer - flush it and try again */

	Flush();
	flush_timer.Cancel();

	/* silently discard datagrams that are larger than max_size */
	return Append(d);
}

void
LogClient::Log(const Datagram &d) noexcept
{
	if (AppendRetry(d) && !flush_timer.IsPending())
		/* wait at most 20ms to accumulate more datagrams
		   before we flush all of them */
		flush_timer.Schedule(std::chrono::milliseconds{20});
}

void
LogClient::Flush() noexcept
{
	assert(buffer_fill > 0);
	assert(n_vecs > 0);

	if (n_vecs == 1) {
		/* if there is only one datagram, use send() which may
		   be faster because the kernel doesn't have to copy
		   those auxiliary buffers from user memory */
		ssize_t nbytes = fd.Send(ToSpan(vecs.front()), MSG_DONTWAIT);
		if (nbytes < 0 && !IsSocketErrorSendWouldBlock(errno))
			logger(1, "Failed to flush access log buffer: ", strerror(errno));
	} else {
		/* batch-send all datagrams with a single sendmmsg()
		   system call */
		std::array<struct mmsghdr, 256> hdrs;
		assert(hdrs.size() == vecs.size());

		for (std::size_t i = 0; i < n_vecs; ++i)
			hdrs[i].msg_hdr = MakeMsgHdr(std::span{vecs}.subspan(i, 1));

		int n = sendmmsg(fd.Get(), hdrs.data(), n_vecs, MSG_DONTWAIT);
		if (n < 0 && !IsSocketErrorSendWouldBlock(errno))
			logger(1, "Failed to flush access log buffer: ", strerror(errno));
	}

	buffer_fill = 0;
	n_vecs = 0;
}
