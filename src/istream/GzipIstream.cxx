// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GzipIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "FacadeIstream.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "lib/zlib/Error.hxx"
#include "util/DestructObserver.hxx"

#include <zlib.h>

#include <stdexcept>

#include <assert.h>

class GzipIstream final : public FacadeIstream, DestructAnchor {
	z_stream z;
	SliceFifoBuffer buffer;

	bool z_initialized = false, z_stream_end = false;

	bool had_input, had_output;

public:
	GzipIstream(struct pool &_pool, UnusedIstreamPtr _input) noexcept
		:FacadeIstream(_pool, std::move(_input))
	{
	}

	~GzipIstream() noexcept override {
		if (z_initialized)
			deflateEnd(&z);
	}

	bool InitZlib() noexcept;

	void Abort(int code, const char *msg) noexcept {
		DestroyError(std::make_exception_ptr(MakeZlibError(code, msg)));
	}

	/**
	 * Submit data from the buffer to our istream handler.
	 *
	 * @return true if at least one byte was consumed, false if
	 * the handler blocks or if the stream was closed
	 */
	bool TryWrite() noexcept;

	/**
	 * Like TryWrite(), but return true only if the handler has
	 * consumed everything had the buffer is now empty.
	 */
	bool TryWriteEverything() noexcept {
		return TryWrite() && buffer.empty();
	}

	/**
	 * Starts to write to the buffer.
	 *
	 * @return a pointer to the writable buffer, or nullptr if there is no
	 * room (our istream handler blocks) or if the stream was closed
	 */
	auto BufferWrite() noexcept {
		buffer.AllocateIfNull(fb_pool_get());
		auto w = buffer.Write();
		if (w.empty() && TryWrite())
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

	off_t _GetAvailable(bool partial) noexcept override {
		return partial || z_stream_end
			? buffer.GetAvailable()
			: -1;
	}

	void _Read() noexcept override {
		if (!buffer.empty() && !TryWriteEverything())
			/* handler wasn't able to consume everything
			   or we were closed: stop here, don't attempt
			   to obtain more data */
			return;

		if (HasInput())
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
		return MAX_WBITS + 16;
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
GzipIstream::InitZlib() noexcept
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

bool
GzipIstream::TryWrite() noexcept
{
	auto r = buffer.Read();
	assert(!r.empty());

	size_t nbytes = InvokeData(r);
	if (nbytes == 0)
		return false;

	buffer.Consume(nbytes);
	buffer.FreeIfEmpty();

	if (nbytes == r.size() && !HasInput() && z_stream_end) {
		DestroyEof();
		return false;
	}

	return true;
}

inline void
GzipIstream::TryFlush() noexcept
{
	assert(z_initialized);
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
GzipIstream::ForceRead() noexcept
{
	const DestructObserver destructed(*this);

	bool had_input2 = false;
	had_output = false;

	while (1) {
		had_input = false;
		input.Read();
		if (destructed)
			return;

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
GzipIstream::TryFinish() noexcept
{
	assert(!HasInput());
	assert(z_initialized);
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
GzipIstream::OnData(const std::span<const std::byte> src) noexcept
{
	assert(HasInput());

	auto w = BufferWrite();
	if (w.size() < 64) /* reserve space for end-of-stream marker */
		return 0;

	if (!InitZlib())
		return 0;

	had_input = true;

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

	return src.size() - (size_t)z.avail_in;
}

void
GzipIstream::OnEof() noexcept
{
	ClearInput();

	if (!InitZlib())
		return;

	TryFinish();
}

void
GzipIstream::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();

	DestroyError(ep);
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
NewGzipIstream(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<GzipIstream>(pool, std::move(input));

}
