/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "istream_fcgi.hxx"
#include "Protocol.hxx"
#include "istream/FacadeIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "util/ByteOrder.hxx"
#include "util/DestructObserver.hxx"

#include <assert.h>
#include <string.h>

class FcgiIstream final : public FacadeIstream, DestructAnchor {
	size_t missing_from_current_record = 0;

	struct fcgi_record_header header;
	size_t header_sent = sizeof(header);

public:
	FcgiIstream(struct pool &_pool, UnusedIstreamPtr _input,
		    uint16_t request_id) noexcept
		:FacadeIstream(_pool, std::move(_input)),
		 header{FCGI_VERSION_1, FCGI_STDIN, request_id}
	{
	}

	bool WriteHeader() noexcept;
	void StartRecord(size_t length) noexcept;
	size_t Feed(const char *data, size_t length) noexcept;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		return partial
			? sizeof(header) - header_sent + input.GetAvailable(partial)
			: -1;
	}

	off_t _Skip(gcc_unused off_t length) noexcept override {
		return -1;
	}

	void _Read() noexcept override;

	int _AsFd() noexcept override {
		return -1;
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *data, size_t length) noexcept override {
		return Feed((const char *)data, length);
	}

	void OnEof() noexcept override;

	void OnError(std::exception_ptr ep) noexcept override {
		ClearInput();
		DestroyError(ep);
	}
};

bool
FcgiIstream::WriteHeader() noexcept
{
	assert(header_sent <= sizeof(header));

	size_t length = sizeof(header) - header_sent;
	if (length == 0)
		return true;

	const char *data = (char *)&header + header_sent;
	size_t nbytes = InvokeData(data, length);
	if (nbytes > 0)
		header_sent += nbytes;

	return nbytes == length;
}

void
FcgiIstream::StartRecord(size_t length) noexcept
{
	assert(missing_from_current_record == 0);
	assert(header_sent == sizeof(header));

	if (length > 0xffff)
		/* uint16_t's limit */
		length = 0xffff;

	header.content_length = ToBE16(length);
	header_sent = 0;
	missing_from_current_record = length;
}

inline size_t
FcgiIstream::Feed(const char *data, size_t length) noexcept
{
	const DestructObserver destructed(*this);

	size_t total = 0;
	while (true) {
		if (!WriteHeader())
			return destructed ? 0 : total;

		if (missing_from_current_record > 0) {
			/* send the record header */
			size_t rest = length - total;
			if (rest > missing_from_current_record)
				rest = missing_from_current_record;

			size_t nbytes = InvokeData(data + total, rest);
			if (nbytes == 0)
				return destructed ? 0 : total;

			assert(!destructed);

			total += nbytes;
			missing_from_current_record -= nbytes;

			if (missing_from_current_record > 0)
				/* not enough data or handler is blocking - return for
				   now */
				return total;
		}

		size_t rest = length - total;
		if (rest == 0)
			return total;

		StartRecord(rest);
	}
}


/*
 * istream handler
 *
 */

void
FcgiIstream::OnEof() noexcept
{
	assert(HasInput());
	assert(missing_from_current_record == 0);
	assert(header_sent == sizeof(header));

	ClearInput();

	/* write EOF record (length 0) */

	StartRecord(0);

	/* flush the buffer */

	if (WriteHeader())
		DestroyEof();
}

/*
 * istream implementation
 *
 */

void
FcgiIstream::_Read() noexcept
{
	if (!WriteHeader())
		return;

	if (!HasInput()) {
		DestroyEof();
		return;
	}

	if (missing_from_current_record == 0) {
		off_t available = input.GetAvailable(true);
		if (available > 0) {
			StartRecord(available);
			if (!WriteHeader())
				return;
		}
	}

	input.Read();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_fcgi_new(struct pool &pool, UnusedIstreamPtr input,
		 uint16_t request_id) noexcept
{
	return NewIstreamPtr<FcgiIstream>(pool, std::move(input),
					  request_id);
}
