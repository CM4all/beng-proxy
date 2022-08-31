/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "istream_deflate.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "FacadeIstream.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "event/DeferEvent.hxx"
#include "util/DestructObserver.hxx"

#include <zlib.h>

#include <stdexcept>

#include <assert.h>

class ZlibError : public std::runtime_error {
	int code;

public:
	explicit ZlibError(int _code, const char *_msg)
		:std::runtime_error(_msg), code(_code) {}

	int GetCode() const noexcept {
		return code;
	}
};

class DeflateIstream final : public FacadeIstream, DestructAnchor {
	const bool gzip;
	bool z_initialized = false, z_stream_end = false;
	z_stream z;
	bool had_input, had_output;
	bool reading = false;
	SliceFifoBuffer buffer;

	/**
	 * This callback is used to request more data from the input if an
	 * OnData() call did not produce any output.  This tries to
	 * prevent stalling the stream.
	 */
	DeferEvent defer;

public:
	DeflateIstream(struct pool &_pool, UnusedIstreamPtr _input,
		       EventLoop &event_loop, bool _gzip) noexcept
		:FacadeIstream(_pool, std::move(_input)),
		 gzip(_gzip),
		 defer(event_loop, BIND_THIS_METHOD(OnDeferred))
	{
	}

	~DeflateIstream() noexcept override {
		if (z_initialized)
			deflateEnd(&z);
	}

	bool InitZlib() noexcept;

	void Abort(int code, const char *msg) noexcept {
		DestroyError(std::make_exception_ptr(ZlibError(code, msg)));
	}

	/**
	 * Submit data from the buffer to our istream handler.
	 *
	 * @return the number of bytes which were handled, or 0 if the
	 * stream was closed
	 */
	size_t TryWrite() noexcept;

	/**
	 * Starts to write to the buffer.
	 *
	 * @return a pointer to the writable buffer, or nullptr if there is no
	 * room (our istream handler blocks) or if the stream was closed
	 */
	auto BufferWrite() noexcept {
		buffer.AllocateIfNull(fb_pool_get());
		auto w = buffer.Write();
		if (w.empty() && TryWrite() > 0)
			w = buffer.Write();

		return w;
	}

	void TryFlush() noexcept;

	/**
	 * Read from our input until we have submitted some bytes to our
	 * istream handler.
	 */
	void ForceRead() noexcept;

	void TryFinish() noexcept;

	/* virtual methods from class Istream */

	void _Read() noexcept override {
		if (!buffer.empty())
			TryWrite();
		else if (HasInput())
			ForceRead();
		else
			TryFinish();
	}

	/* virtual methods from class IstreamHandler */
	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

private:
	int GetWindowBits() const noexcept {
		return MAX_WBITS + gzip * 16;
	}

	void OnDeferred() noexcept {
		assert(HasInput());

		ForceRead();
	}
};

static voidpf
z_alloc(voidpf opaque, uInt items, uInt size) noexcept
{
	struct pool *pool = (struct pool *)opaque;

	return p_malloc(pool, items * size);
}

static void
z_free(voidpf opaque, voidpf address) noexcept
{
	(void)opaque;
	(void)address;
}

bool
DeflateIstream::InitZlib() noexcept
{
	if (z_initialized)
		return true;

	z.zalloc = z_alloc;
	z.zfree = z_free;
	z.opaque = &GetPool();

	int err = deflateInit2(&z, Z_DEFAULT_COMPRESSION,
			       Z_DEFLATED, GetWindowBits(), 8,
			       Z_DEFAULT_STRATEGY);
	if (err != Z_OK) {
		Abort(err, "deflateInit2() failed");
		return false;
	}

	z_initialized = true;
	return true;
}

size_t
DeflateIstream::TryWrite() noexcept
{
	auto r = buffer.Read();
	assert(!r.empty());

	size_t nbytes = InvokeData(r);
	if (nbytes == 0)
		return 0;

	buffer.Consume(nbytes);
	buffer.FreeIfEmpty();

	if (nbytes == r.size() && !HasInput() && z_stream_end) {
		DestroyEof();
		return 0;
	}

	return nbytes;
}

inline void
DeflateIstream::TryFlush() noexcept
{
	assert(!z_stream_end);

	auto w = BufferWrite();
	if (w.empty())
		return;

	z.next_out = (Bytef *)w.data();
	z.avail_out = (uInt)w.size();

	z.next_in = nullptr;
	z.avail_in = 0;

	int err = deflate(&z, Z_SYNC_FLUSH);
	if (err != Z_OK) {
		Abort(err, "deflate(Z_SYNC_FLUSH) failed");
		return;
	}

	buffer.Append(w.size() - (size_t)z.avail_out);

	if (!buffer.empty())
		TryWrite();
}

inline void
DeflateIstream::ForceRead() noexcept
{
	assert(!reading);

	const DestructObserver destructed(*this);

	bool had_input2 = false;
	had_output = false;

	while (1) {
		had_input = false;
		reading = true;
		input.Read();
		if (destructed)
			return;

		reading = false;
		if (!HasInput() || had_output)
			return;

		if (!had_input)
			break;

		had_input2 = true;
	}

	if (had_input2)
		TryFlush();
}

void
DeflateIstream::TryFinish() noexcept
{
	assert(!z_stream_end);

	auto w = BufferWrite();
	if (w.empty())
		return;

	z.next_out = (Bytef *)w.data();
	z.avail_out = (uInt)w.size();

	z.next_in = nullptr;
	z.avail_in = 0;

	int err = deflate(&z, Z_FINISH);
	if (err == Z_STREAM_END)
		z_stream_end = true;
	else if (err != Z_OK) {
		Abort(err, "deflate(Z_FINISH) failed");
		return;
	}

	buffer.Append(w.size() - (size_t)z.avail_out);

	if (z_stream_end && buffer.empty()) {
		DestroyEof();
	} else
		TryWrite();
}


/*
 * istream handler
 *
 */

size_t
DeflateIstream::OnData(const std::span<const std::byte> src) noexcept
{
	assert(HasInput());

	auto w = BufferWrite();
	if (w.size() < 64) /* reserve space for end-of-stream marker */
		return 0;

	if (!InitZlib())
		return 0;

	had_input = true;

	if (!reading)
		had_output = false;

	z.next_out = (Bytef *)w.data();
	z.avail_out = (uInt)w.size();

	z.next_in = (Bytef *)const_cast<std::byte *>(src.data());
	z.avail_in = (uInt)src.size();

	do {
		auto err = deflate(&z, Z_NO_FLUSH);
		if (err != Z_OK) {
			Abort(err, "deflate() failed");
			return 0;
		}

		size_t nbytes = w.size() - (size_t)z.avail_out;
		if (nbytes > 0) {
			had_output = true;
			buffer.Append(nbytes);

			const DestructObserver destructed(*this);
			TryWrite();
			if (destructed)
				return 0;
		} else
			break;

		w = BufferWrite();
		if (w.size() < 64) /* reserve space for end-of-stream marker */
			break;

		z.next_out = (Bytef *)w.data();
		z.avail_out = (uInt)w.size();
	} while (z.avail_in > 0);

	if (!reading && !had_output)
		/* we received data from our input, but we did not produce any
		   output (and we're not looping inside ForceRead()) - to
		   avoid stalling the stream, trigger the DeferEvent */
		defer.Schedule();

	return src.size() - (size_t)z.avail_in;
}

void
DeflateIstream::OnEof() noexcept
{
	ClearInput();
	defer.Cancel();

	if (!InitZlib())
		return;

	TryFinish();
}

void
DeflateIstream::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();

	DestroyError(ep);
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_deflate_new(struct pool &pool, UnusedIstreamPtr input,
		    EventLoop &event_loop, bool gzip) noexcept
{
	return NewIstreamPtr<DeflateIstream>(pool, std::move(input),
					     event_loop, gzip);

}
