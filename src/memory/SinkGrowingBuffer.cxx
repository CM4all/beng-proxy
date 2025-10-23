// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SinkGrowingBuffer.hxx"
#include "istream/Bucket.hxx"
#include "io/FileDescriptor.hxx"

IstreamReadyResult
GrowingBufferSink::OnIstreamReady() noexcept
{
	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		handler.OnGrowingBufferSinkError(std::current_exception());
		return IstreamReadyResult::CLOSED;
	}

	std::size_t nbytes = 0;
	bool more = list.HasMore();

	for (const auto &bucket : list) {
		if (!bucket.IsBuffer()) {
			more = true;
			break;
		}

		auto r = bucket.GetBuffer();
		buffer.Write(r);
		nbytes += r.size();
	}

	const bool eof =  nbytes > 0
		? input.ConsumeBucketList(nbytes).eof
		: !more;

	if (eof) {
		CloseInput();
		handler.OnGrowingBufferSinkEof(std::move(buffer));
		return IstreamReadyResult::CLOSED;
	}

	return IstreamReadyResult::OK;
}

std::size_t
GrowingBufferSink::OnData(std::span<const std::byte> src) noexcept
{
	buffer.Write(src);
	return src.size();
}

IstreamDirectResult
GrowingBufferSink::OnDirect(FdType, FileDescriptor fd, off_t offset,
			    std::size_t max_length,
			    [[maybe_unused]] bool then_eof) noexcept
{
	auto w = buffer.BeginWrite();
	if (w.size() > max_length)
		w = w.first(max_length);

	ssize_t nbytes = HasOffset(offset)
		? fd.ReadAt(offset, w)
		: fd.Read(w);
	if (nbytes <= 0)
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	input.ConsumeDirect(nbytes);
	buffer.CommitWrite(nbytes);

	return IstreamDirectResult::OK;
}

void
GrowingBufferSink::OnEof() noexcept
{
	input.Clear();
	handler.OnGrowingBufferSinkEof(std::move(buffer));
}

void
GrowingBufferSink::OnError(std::exception_ptr &&error) noexcept
{
	input.Clear();
	handler.OnGrowingBufferSinkError(std::move(error));
}
