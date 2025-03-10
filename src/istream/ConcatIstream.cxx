// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ConcatIstream.hxx"
#include "Sink.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "io/FileDescriptor.hxx"
#include "util/DestructObserver.hxx"
#include "util/IntrusiveList.hxx"

#include <assert.h>

class CatIstream final : public Istream, DestructAnchor {
	struct Input final
		: IstreamSink, IntrusiveListHook<IntrusiveHookMode::NORMAL>
	{
		CatIstream &cat;

		Input(CatIstream &_cat, UnusedIstreamPtr &&_istream) noexcept
			:IstreamSink(std::move(_istream)), cat(_cat) {}

		void SetDirect(FdTypeMask direct) noexcept {
			input.SetDirect(direct);
		}

		off_t GetAvailable(bool partial) const noexcept {
			return input.GetAvailable(partial);
		}

		off_t Skip(off_t length) noexcept {
			return input.Skip(length);
		}

		void Read() noexcept {
			input.Read();
		}

		void FillBucketList(IstreamBucketList &list) {
			input.FillBucketList(list);
		}

		auto ConsumeBucketList(std::size_t nbytes) noexcept {
			return input.ConsumeBucketList(nbytes);
		}

		void ConsumeDirect(std::size_t nbytes) noexcept {
			return input.ConsumeDirect(nbytes);
		}

		/* virtual methods from class IstreamHandler */

		IstreamReadyResult OnIstreamReady() noexcept override {
			assert(input.IsDefined());

			return cat.OnInputReady(*this);
		}

		std::size_t OnData(std::span<const std::byte> src) noexcept override {
			assert(input.IsDefined());

			return cat.OnInputData(*this, src);
		}

		IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
					     off_t offset, std::size_t max_length,
					     bool then_eof) noexcept override {
			assert(input.IsDefined());

			return cat.OnInputDirect(*this, type, fd, offset,
						 max_length, then_eof);
		}

		void OnEof() noexcept override {
			assert(input.IsDefined());
			ClearInput();

			cat.OnInputEof(*this);
		}

		void OnError(std::exception_ptr ep) noexcept override {
			assert(input.IsDefined());
			ClearInput();

			cat.OnInputError(*this, ep);
		}

		struct Disposer {
			void operator()(Input *input) noexcept {
				input->~Input();
			}
		};
	};

	bool reading = false;

	/**
	 * Has OnInputReady() been called at least once?
	 */
	bool seen_ready = false;

	using InputList = IntrusiveList<Input>;
	InputList inputs;

public:
	CatIstream(struct pool &p, std::span<UnusedIstreamPtr> _inputs) noexcept;

	~CatIstream() noexcept override {
		inputs.clear_and_dispose(Input::Disposer());
	}

	void Append(UnusedIstreamPtr &&istream) noexcept {
		auto *input = NewFromPool<Input>(GetPool(), *this,
						 std::move(istream));
		inputs.push_back(*input);
	}

private:
	Input &GetCurrent() noexcept {
		return inputs.front();
	}

	const Input &GetCurrent() const noexcept {
		return inputs.front();
	}

	bool IsCurrent(const Input &input) const noexcept {
		return !inputs.empty() && &GetCurrent() == &input;
	}

	bool IsLast() const noexcept {
		assert(!inputs.empty());

		return std::next(inputs.begin()) == inputs.end();
	}

	[[gnu::pure]]
	bool HasInput(const Input &input) const noexcept {
		return std::any_of(inputs.begin(), inputs.end(), [&input](const auto &i){
			return &i == &input;
		});
	}

	bool IsEOF() const noexcept {
		return inputs.empty();
	}

	IstreamReadyResult OnInputReady(Input &i) noexcept;

	std::size_t OnInputData(Input &i, std::span<const std::byte> src) noexcept {
		return IsCurrent(i)
			? InvokeData(src)
			: 0;
	}

	IstreamDirectResult OnInputDirect(Input &i,
					  FdType type, FileDescriptor fd,
					  off_t offset, std::size_t max_length,
					  bool then_eof) noexcept {
		return IsCurrent(i)
			? InvokeDirect(type, fd, offset, max_length,
				       then_eof && IsLast())
			: IstreamDirectResult::BLOCKING;
	}

	void OnInputEof(Input &i) noexcept {
		const bool current = IsCurrent(i);
		i.unlink();

		if (IsEOF()) {
			assert(current);
			DestroyEof();
		} else if (current && !reading) {
			/* only call Input::_Read() if this function was not called
			   from CatIstream:Read() - in this case,
			   istream_cat_read() would provide the loop.  This is
			   advantageous because we avoid unnecessary recursing. */
			GetCurrent().Read();
		}
	}

	void OnInputError(Input &i, std::exception_ptr ep) noexcept {
		i.unlink();
		DestroyError(ep);
	}

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		for (auto &i : inputs)
			i.SetDirect(mask);
	}

	off_t _GetAvailable(bool partial) noexcept override;
	off_t _Skip([[maybe_unused]] off_t length) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;
};

inline IstreamReadyResult
CatIstream::OnInputReady(Input &i) noexcept
{
	const bool is_current = IsCurrent(i);
	if (!seen_ready)
		/* if this method is being called for the first time,
		   we skip the IsCurrent() check and assume previous
		   inputs are ready as well; in some cases, this
		   avoids unnecessary epoll_ctl() system calls */
		seen_ready = true;
	else if (!is_current)
		return IstreamReadyResult::OK;

	auto result = InvokeReady();
	if (result != IstreamReadyResult::CLOSED &&
	    !(is_current ? IsCurrent(i) : HasInput(i)))
		/* the input that is ready has meanwhile been
		   closed */
		result = IstreamReadyResult::CLOSED;

	return result;
}

/*
 * istream implementation
 *
 */

off_t
CatIstream::_GetAvailable(bool partial) noexcept
{
	off_t available = 0;

	for (const auto &input : inputs) {
		const off_t a = input.GetAvailable(partial);
		if (a != (off_t)-1)
			available += a;
		else if (!partial)
			/* if the caller wants the exact number of bytes, and
			   one input cannot provide it, we cannot provide it
			   either */
			return (off_t)-1;
	}

	return available;
}

off_t
CatIstream::_Skip(off_t length) noexcept
{
	if (inputs.empty())
		return 0;

	off_t nbytes = inputs.front().Skip(length);
	Consumed(nbytes);
	return nbytes;
}

void
CatIstream::_Read() noexcept
{
	if (IsEOF()) {
		DestroyEof();
		return;
	}

	const DestructObserver destructed(*this);

	reading = true;

	CatIstream::InputList::iterator prev;
	do {
		prev = inputs.begin();
		GetCurrent().Read();
		if (destructed)
			return;
	} while (!IsEOF() && inputs.begin() != prev);

	reading = false;
}

void
CatIstream::_FillBucketList(IstreamBucketList &list)
{
	assert(!list.HasMore());

	for (auto i = inputs.begin(); i != inputs.end();) {
		auto &input = *i;

		try {
			const auto m = list.Mark();

			input.FillBucketList(list);

			if (list.EmptySinceMark(m)) {
				/* this input hasn't added any data to
				   the list and hasn't set the "more"
				   flag, so we can assume it's EOF */
				i = inputs.erase_and_dispose(i, Input::Disposer{});
				continue;
			}
		} catch (...) {
			input.unlink();
			Destroy();
			throw;
		}

		if (list.HasMore())
			break;

		++i;
	}
}

Istream::ConsumeBucketResult
CatIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	std::size_t total = 0;

	while (!inputs.empty()) {
		auto &input = inputs.front();

		const auto r = input.ConsumeBucketList(nbytes);
		if (r.eof)
			inputs.erase_and_dispose(inputs.iterator_to(input),
						 Input::Disposer{});

		Consumed(r.consumed);
		total += r.consumed;
		nbytes -= r.consumed;

		if (nbytes == 0)
			break;

		/* if there is still data to be consumed, then the
		   current input must have reached EOF */
		assert(r.eof);
	}

	return {total, inputs.empty()};
}

void
CatIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	GetCurrent().ConsumeDirect(nbytes);
}

/*
 * constructor
 *
 */

inline CatIstream::CatIstream(struct pool &p,
			      std::span<UnusedIstreamPtr> _inputs) noexcept
	:Istream(p)
{
	for (UnusedIstreamPtr &_input : _inputs) {
		if (!_input)
			continue;

		Append(std::move(_input));
	}
}

UnusedIstreamPtr
_NewConcatIstream(struct pool &pool, std::span<UnusedIstreamPtr> inputs)
{
	return UnusedIstreamPtr{NewFromPool<CatIstream>(pool, pool, inputs)};
}

void
AppendConcatIstream(UnusedIstreamPtr &_cat, UnusedIstreamPtr istream) noexcept
{
	auto &cat = _cat.StaticCast<CatIstream>();
	cat.Append(std::move(istream));
}
