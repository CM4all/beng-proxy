// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Istream.hxx"
#include "Class.hxx"
#include "istream/FacadeIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "util/SpanCast.hxx"
#include "util/DestructObserver.hxx"

#include <assert.h>

class EscapeIstream final : public FacadeIstream, DestructAnchor {
	const struct escape_class &cls;

	std::string_view escaped{};

public:
	EscapeIstream(struct pool &_pool, UnusedIstreamPtr _input,
		      const struct escape_class &_cls) noexcept
		:FacadeIstream(_pool, std::move(_input)),
		 cls(_cls) {
	}

	bool SendEscaped() noexcept;

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override {
		IstreamLength result{
			.length = static_cast<off_t>(escaped.size()),
			.exhaustive = true,
		};

		if (HasInput()) {
			result.exhaustive = false;
			result += input.GetLength();
		}

		return result;
	}

	void _Read() noexcept override;
	void _Close() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;

	void OnEof() noexcept override {
		ClearInput();

		if (escaped.empty())
			DestroyEof();
	}

	void OnError(std::exception_ptr &&ep) noexcept override {
		ClearInput();
		DestroyError(std::move(ep));
	}
};

bool
EscapeIstream::SendEscaped() noexcept
{
	assert(!escaped.empty());

	size_t nbytes = InvokeData(AsBytes(escaped));
	if (nbytes == 0)
		return false;

	escaped = escaped.substr(nbytes);
	if (!escaped.empty())
		return false;

	if (!HasInput()) {
		DestroyEof();
		return false;
	}

	return true;
}

/*
 * istream handler
 *
 */

std::size_t
EscapeIstream::OnData(const std::span<const std::byte> src) noexcept
{
	const char *data = (const char *)src.data();
	std::size_t length = src.size();

	if (!escaped.empty() && !SendEscaped())
		return 0;

	size_t total = 0;

	const DestructObserver destructed(*this);

	do {
		/* find the next control character */
		const char *control = escape_find(&cls, {data, length});
		if (control == nullptr) {
			/* none found - just forward the data block to our sink */
			size_t nbytes = InvokeData(std::as_bytes(std::span{data, length}));
			if (destructed)
				return 0;

			total += nbytes;
			break;
		}

		if (control > data) {
			/* forward the portion before the control character */
			const size_t n = control - data;
			size_t nbytes = InvokeData(std::as_bytes(std::span{data, n}));
			if (destructed)
				return 0;

			total += nbytes;
			if (nbytes < n)
				break;
		}

		/* consume everything until after the control character */

		length -= control - data + 1;
		data = control + 1;
		++total;

		/* insert the entity into the stream */

		escaped = escape_char(&cls, *control);

		if (!SendEscaped()) {
			if (destructed)
				return 0;
			break;
		}
	} while (length > 0);

	return total;
}

/*
 * istream implementation
 *
 */

void
EscapeIstream::_Read() noexcept
{
	if (!escaped.empty() && !SendEscaped())
		return;

	input.Read();
}

void
EscapeIstream::_Close() noexcept
{
	Destroy();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_escape_new(struct pool &pool, UnusedIstreamPtr input,
		   const struct escape_class &cls)
{
	assert(cls.escape_find != nullptr);
	assert(cls.escape_char != nullptr);

	return NewIstreamPtr<EscapeIstream>(pool, std::move(input), cls);
}
