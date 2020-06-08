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

#include "Distribute.hxx"
#include "net/SocketAddress.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

ControlDistribute::ControlDistribute(EventLoop &event_loop,
				     ControlHandler &_next_handler)
	:distribute(event_loop),
	 next_handler(_next_handler)
{
}

bool
ControlDistribute::OnControlRaw(ConstBuffer<void> payload,
				SocketAddress address, int uid)
{
	/* forward the packet to all worker processes */
	distribute.Packet(payload.data, payload.size);

	return next_handler.OnControlRaw(payload, address, uid);
}

void
ControlDistribute::OnControlPacket(ControlServer &control_server,
				   BengProxy::ControlCommand command,
				   ConstBuffer<void> payload,
				   WritableBuffer<UniqueFileDescriptor> fds,
				   SocketAddress address, int uid)
{
	return next_handler.OnControlPacket(control_server, command,
					    payload, fds, address, uid);
}

void
ControlDistribute::OnControlError(std::exception_ptr ep) noexcept
{
	return next_handler.OnControlError(ep);
}
