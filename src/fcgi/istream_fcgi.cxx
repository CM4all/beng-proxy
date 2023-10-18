// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
		 header{
			 .version = FCGI_VERSION_1,
			 .type = FcgiRecordType::STDIN,
			 .request_id = request_id,
		 }
	{
	}

	bool WriteHeader() noexcept;
	void StartRecord(size_t length) noexcept;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		return partial
			? sizeof(header) - header_sent + input.GetAvailable(partial)
			: -1;
	}

	off_t _Skip([[maybe_unused]] off_t length) noexcept override {
		return -1;
	}

	void _Read() noexcept override;

	int _AsFd() noexcept override {
		return -1;
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override;

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

	const std::byte *data = (std::byte *)&header + header_sent;
	size_t nbytes = InvokeData({data, length});
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

	header.content_length = length;
	header_sent = 0;
	missing_from_current_record = length;
}

std::size_t
FcgiIstream::OnData(const std::span<const std::byte> src) noexcept
{
	const DestructObserver destructed(*this);

	size_t total = 0;
	while (true) {
		if (!WriteHeader())
			return destructed ? 0 : total;

		if (missing_from_current_record > 0) {
			/* send the record header */
			size_t rest = src.size() - total;
			if (rest > missing_from_current_record)
				rest = missing_from_current_record;

			size_t nbytes = InvokeData({src.data() + total, rest});
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

		size_t rest = src.size() - total;
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
