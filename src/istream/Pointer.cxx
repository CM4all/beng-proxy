// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Pointer.hxx"
#include "UnusedPtr.hxx"

IstreamPointer::IstreamPointer(UnusedIstreamPtr src,
			       IstreamHandler &handler) noexcept
	:stream(src.Steal())
{
	if (stream != nullptr)
		stream->SetHandler(handler);
}

void
IstreamPointer::Set(UnusedIstreamPtr _stream,
		    IstreamHandler &handler) noexcept
{
	assert(!IsDefined());
	assert(_stream);

	stream = _stream.Steal();
	stream->SetHandler(handler);
}

UnusedIstreamPtr
IstreamPointer::Steal() noexcept
{
	if (stream != nullptr)
		stream->ClearHandler();
	return UnusedIstreamPtr(std::exchange(stream, nullptr));
}
