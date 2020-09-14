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

#include "istream_iconv.hxx"
#include "FacadeIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "util/DestructObserver.hxx"

#include <iconv.h>

#include <stdexcept>

#include <assert.h>
#include <errno.h>

class IconvIstream final : public FacadeIstream, DestructAnchor {
	const iconv_t iconv;
	SliceFifoBuffer buffer;

public:
	IconvIstream(struct pool &p, UnusedIstreamPtr _input,
		     iconv_t _iconv) noexcept
		:FacadeIstream(p, std::move(_input)),
		 iconv(_iconv)
	{
	}

	~IconvIstream() noexcept {
		iconv_close(iconv);
	}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		if (partial)
			return buffer.GetAvailable();

		return -1;
	}

	void _Read() noexcept override;
	void _Close() noexcept override;

	/* handler */

	size_t OnData(const void *data, size_t length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

static inline size_t
deconst_iconv(iconv_t cd,
	      const char **inbuf, size_t *inbytesleft,
	      char **outbuf, size_t *outbytesleft) noexcept
{
	char **inbuf2 = const_cast<char **>(inbuf);
	return iconv(cd, inbuf2, inbytesleft, outbuf, outbytesleft);
}

/*
 * istream handler
 *
 */

size_t
IconvIstream::OnData(const void *_data, size_t length) noexcept
{
	assert(input.IsDefined());

	const DestructObserver destructed(*this);

	buffer.AllocateIfNull(fb_pool_get());

	const char *data = (const char *)_data, *src = data;

	do {
		auto w = buffer.Write();
		if (w.empty()) {
			/* no space left in the buffer: attempt to flush it */

			size_t nbytes = SendFromBuffer(buffer);
			if (nbytes == 0) {
				if (destructed)
					return 0;
				break;
			}

			assert(!destructed);

			continue;
		}

		char *const dest0 = (char *)w.data;
		char *dest = dest0;
		size_t dest_left = w.size;

		size_t ret = deconst_iconv(iconv, &src, &length, &dest, &dest_left);
		if (dest > dest0)
			buffer.Append(dest - dest0);

		if (ret == (size_t)-1) {
			switch (errno) {
				size_t nbytes;

			case EILSEQ:
				/* invalid sequence: skip this byte */
				++src;
				--length;
				break;

			case EINVAL:
				/* incomplete sequence: leave it in the buffer */
				if (src == data) {
					/* XXX we abort here, because we believe if the
					   incomplete sequence is at the start of the
					   buffer, this might be EOF; we should rather
					   buffer this incomplete sequence and report the
					   caller that we consumed it */
					input.ClearAndClose();

					DestroyError(std::make_exception_ptr(std::runtime_error("incomplete sequence")));
					return 0;
				}

				length = 0;
				break;

			case E2BIG:
				/* output buffer is full: flush dest */
				nbytes = SendFromBuffer(buffer);
				if (nbytes == 0) {
					if (destructed)
						return 0;

					/* reset length to 0, to make the loop quit
					   (there's no "double break" to break out of the
					   while loop in C) */
					length = 0;
					break;
				}

				assert(!destructed);
				break;
			}
		}
	} while (length > 0);

	SendFromBuffer(buffer);
	if (destructed)
		return 0;

	buffer.FreeIfEmpty();

	return src - data;
}

void
IconvIstream::OnEof() noexcept
{
	assert(input.IsDefined());
	input.Clear();

	if (buffer.empty())
		DestroyEof();
}

void
IconvIstream::OnError(std::exception_ptr ep) noexcept
{
	assert(input.IsDefined());

	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

void
IconvIstream::_Read() noexcept
{
	if (input.IsDefined())
		input.Read();
	else {
		size_t rest = ConsumeFromBuffer(buffer);
		if (rest == 0)
			DestroyEof();
	}
}

void
IconvIstream::_Close() noexcept
{
	if (input.IsDefined())
		input.ClearAndClose();
	Destroy();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_iconv_new(struct pool &pool, UnusedIstreamPtr input,
		  const char *tocode, const char *fromcode) noexcept
{
	const iconv_t iconv = iconv_open(tocode, fromcode);
	if (iconv == (iconv_t)-1)
		return nullptr;

	return NewIstreamPtr<IconvIstream>(pool, std::move(input), iconv);
}
