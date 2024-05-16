// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ThreadIstream.hxx"
#include "FacadeIstream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "UnusedPtr.hxx"
#include "thread/Job.hxx"
#include "thread/Queue.hxx"
#include "event/DeferEvent.hxx"
#include "memory/fb_pool.hxx"
#include "util/LeakDetector.hxx"

#include <memory>

inline
ThreadIstreamInternal::~ThreadIstreamInternal() noexcept
{
	input.FreeIfDefined();
	output.FreeIfDefined();
}

class ThreadIstream final : public FacadeIstream {
	ThreadQueue &queue;

	SliceFifoBuffer unprotected_output;

	/**
	 * This event defers the Istream::InvokeReady() call which is
	 * necessary because _ConsumeBucketList() is not allowed to
	 * call it.
	 */
	DeferEvent defer_ready;

	struct Internal final : ThreadIstreamInternal, ThreadJob, ::LeakDetector {
		ThreadIstream &istream;

		std::unique_ptr<ThreadIstreamFilter> filter;

		/**
		 * If this is set, an exception was caught inside the thread, and
		 * shall be forwarded to the main thread.
		 */
		std::exception_ptr error;

		/**
		 * True when #output and #unprotected_output were
		 * full.  This will schedule another Run() call as
		 * soon as some data from #unprotected_output gets
		 * consumed so the filter can continue to fill the
		 * output buffer.
		 */
		bool output_full = false;

		bool postponed_destroy = false;

		Internal(ThreadIstream &_istream,
			 std::unique_ptr<ThreadIstreamFilter> &&_filter) noexcept
			:istream(_istream), filter(std::move(_filter)) {
			assert(filter);
		}

		void PreRun() noexcept {
			assert(filter);

			{
				const std::scoped_lock lock{mutex};
				istream.unprotected_output.MoveFromAllowNull(output);
				output.AllocateIfNull(fb_pool_get());
			}

			filter->PreRun(*this);
		}

		void PostRun() noexcept {
			assert(filter);

			filter->PostRun(*this);
		}

		void Schedule() noexcept {
			assert(filter);

			PreRun();
			istream.queue.Add(*this);
		}

		/**
		 * @return the number of bytes appended and a boolean
		 * specfiying whether the #input was empty
		 */
		std::pair<std::size_t, bool> LockAppendInput(std::span<const std::byte> src) noexcept {
			const std::scoped_lock lock{mutex};
			const bool was_empty = input.empty();

			if (input.IsNull())
				input.Allocate(fb_pool_get());

			auto w = input.Write();
			if (w.size() < src.size())
				src = src.first(w.size());

			std::copy(src.begin(), src.end(), w.begin());
			input.Append(src.size());

			return {src.size(), was_empty};
		}

		void CancelPostponeDestroy() noexcept {
			assert(!postponed_destroy);
			postponed_destroy = true;
			filter->CancelRun(*this);
		}

		// virtual methods from ThreadJob
		void Run() noexcept override;
		void Done() noexcept override;
	};

	std::unique_ptr<Internal> internal;

public:
	ThreadIstream(struct pool &_pool, ThreadQueue &_queue,
		      UnusedIstreamPtr &&_input,
		      std::unique_ptr<ThreadIstreamFilter> &&_filter) noexcept
		:FacadeIstream(_pool, std::move(_input)),
		 queue(_queue),
		 defer_ready(queue.GetEventLoop(), BIND_THIS_METHOD(OnDeferredReady)),
		 internal(std::make_unique<Internal>(*this, std::move(_filter))) {}

	~ThreadIstream() noexcept override;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		// TODO special case at EOF?

		if (!partial && internal)
			return -1;

		return unprotected_output.GetAvailable();
	}

	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;

	/* virtual methods from class IstreamHandler */
	IstreamReadyResult OnIstreamReady() noexcept override;
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr error) noexcept override;

private:
	void OnDeferredReady() noexcept;

	/**
	 * Invoke FillBucketList() on our input and move it to the
	 * input buffer.
	 *
	 * Throws on error.
	 */
	IstreamReadyResult ReadBucketsFromInput();

	void MaybeFillInput() noexcept;

	/**
	 * @return true if the #unprotected_output is now empty, false
	 * if it is non-empty (or if this object has been destroyed)
	 */
	bool SubmitOutput() noexcept;

	/**
	 * Data from #unprotected_output was just consumed, and this
	 * method tries to refill it.
	 *
	 * @return true if more data was added to #unprotected_output
	 */
	[[nodiscard]]
	bool OutputConsumed() noexcept;
};

void
ThreadIstream::Internal::Done() noexcept
{
	assert(filter);

	if (postponed_destroy) {
		/* the object has been closed, and now that the thread
		   has finished, we can finally destroy it */
		delete this;
		return;
	}

	if (error) {
		istream.DestroyError(std::move(error));
		return;
	}

	bool output_empty, input_empty, input_full, _again;

	{
		const std::scoped_lock lock{mutex};
		istream.unprotected_output.MoveFromAllowNull(output);
		output_empty = output.empty();
		output_full = output.IsDefinedAndFull();
		output.FreeIfEmpty();
		input_empty = input.empty();
		input_full = input.IsDefinedAndFull();
		input.FreeIfEmpty();
		_again = ThreadIstreamInternal::again || ThreadJob::again;
		ThreadIstreamInternal::again = false;
	}

	if (_again && !output_full)
		Schedule();
	else
		PostRun();

	/* copy reference to stack because the following block may
	   destroy this object */
	auto &_istream = istream;

	bool destroyed = false;
	if (!has_input && input_empty && drained && !_again) {
		/* there is no more input and the filter's output
		   buffers are drained: we don't need it anymore, we
		   already have all we need */
		filter.reset();

		/* if the output buffer is also empty: we can destroy
		   this Internal instance as well */
		if (output_empty) {
			_istream.internal.reset();
			destroyed = true;
		}
	}

	/* submit the output buffer to the IstreamHandler */
	if (destroyed || !_istream.unprotected_output.empty()) {
		switch (_istream.InvokeReady()) {
		case IstreamReadyResult::OK:
			break;

		case IstreamReadyResult::FALLBACK:
			if (!_istream.SubmitOutput())
				return;
			break;

		case IstreamReadyResult::CLOSED:
			return;
		}
	}

	if (!input_full)
		_istream.MaybeFillInput();
}

void
ThreadIstream::Internal::Run() noexcept
{
	assert(filter);

	filter->Run(*this);
}

ThreadIstream::~ThreadIstream() noexcept
{
	if (internal && !queue.Cancel(*internal))
		internal.release()->CancelPostponeDestroy();

	unprotected_output.FreeIfDefined();
}

inline void
ThreadIstream::OnDeferredReady() noexcept
{
	if (unprotected_output.empty())
		return;

	switch (InvokeReady()) {
	case IstreamReadyResult::OK:
		break;

	case IstreamReadyResult::FALLBACK:
		if (!SubmitOutput())
			return;
		break;

	case IstreamReadyResult::CLOSED:
		return;
	}
}

IstreamReadyResult
ThreadIstream::ReadBucketsFromInput()
{
	IstreamBucketList list;
	input.FillBucketList(list);

	std::size_t nbytes = 0;
	IstreamReadyResult result = IstreamReadyResult::OK;
	bool more = list.HasMore(), schedule = false;

	for (const auto &bucket : list) {
		if (!bucket.IsBuffer()) {
			result = IstreamReadyResult::FALLBACK;
			more = true;
			break;
		}

		auto src = bucket.GetBuffer();
		const auto [n_copy, was_empty] = internal->LockAppendInput(src);
		if (was_empty && n_copy > 0)
			schedule = true;

		nbytes += n_copy;

		if (n_copy < src.size()) {
			more = true;
			break;
		}
	}

	if (nbytes > 0)
		input.ConsumeBucketList(nbytes);

	if (!more) {
		CloseInput();
		internal->has_input = false;
		schedule = true;
		result = IstreamReadyResult::CLOSED;
	}

	if (schedule)
		internal->Schedule();

	if (list.ShouldFallback()) {
		assert(more);
		result = IstreamReadyResult::FALLBACK;
	}

	return result;

}

void
ThreadIstream::MaybeFillInput() noexcept
{
	if (!HasInput())
		return;

	try {
		switch (ReadBucketsFromInput()) {
		case IstreamReadyResult::OK:
			break;

		case IstreamReadyResult::FALLBACK:
			input.Read();
			break;

		case IstreamReadyResult::CLOSED:
			return;
		}
	} catch (...) {
		DestroyError(std::current_exception());
	}
}

bool
ThreadIstream::SubmitOutput() noexcept
{
	bool again;

	do {
		again = false;

		if (const auto r = unprotected_output.Read(); !r.empty()) {
			const auto nbytes = InvokeData(r);
			if (nbytes > 0) {
				unprotected_output.Consume(nbytes);
				again = OutputConsumed();
			}

			if (nbytes < r.size())
				return false;
		}
	} while (again);

	if (!internal) {
		DestroyEof();
		return false;
	}

	return true;
}

bool
ThreadIstream::OutputConsumed() noexcept
{
	assert(unprotected_output.IsDefined());
	assert(!unprotected_output.IsFull());

	unprotected_output.FreeIfEmpty();

	if (!internal)
		return false;

	bool dispose_internal;

	{
		const std::scoped_lock lock{internal->mutex};
		if (internal->output.empty()) {
			assert(!internal->output_full);
			return false;
		}

		unprotected_output.MoveFromAllowNull(internal->output);

		dispose_internal = internal->IsIdle() && internal->output.empty() && internal->input.empty() && !internal->has_input && internal->drained;
	}

	if (dispose_internal) {
		internal.reset();
		return true;
	}

	if (internal->output_full) {
		internal->output_full = false;
		internal->Schedule();
	}

	return true;
}

void
ThreadIstream::_Read() noexcept
{
	if (!SubmitOutput())
		return;

	if (HasInput())
		input.Read();
}

void
ThreadIstream::_FillBucketList(IstreamBucketList &list)
{
	if (const auto r = unprotected_output.Read(); !r.empty())
		list.Push(r);
	else if (HasInput()) {
		try {
			switch (ReadBucketsFromInput()) {
			case IstreamReadyResult::OK:
			case IstreamReadyResult::CLOSED:
				break;

			case IstreamReadyResult::FALLBACK:
				list.EnableFallback();
				break;
			}
		} catch (...) {
			Destroy();
			throw;
		}
	}

	if (internal)
		list.SetMore();
}

Istream::ConsumeBucketResult
ThreadIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	const std::size_t available = unprotected_output.GetAvailable();
	if (nbytes > available)
		nbytes = available;

	unprotected_output.Consume(nbytes);

	bool is_eof = false;
	if (OutputConsumed())
		defer_ready.Schedule();
	else
		is_eof = nbytes == available && !internal;

	return {Consumed(nbytes), is_eof};
}

IstreamReadyResult
ThreadIstream::OnIstreamReady() noexcept
{
	try {
		return ReadBucketsFromInput();
	} catch (...) {
		DestroyError(std::current_exception());
		return IstreamReadyResult::CLOSED;
	}
}

std::size_t
ThreadIstream::OnData(std::span<const std::byte> src) noexcept
{
	assert(internal);

	const auto [nbytes, was_empty] = internal->LockAppendInput(src);
	if (was_empty && nbytes > 0)
		internal->Schedule();

	return nbytes;
}

void
ThreadIstream::OnEof() noexcept
{
	assert(internal);
	assert(internal->has_input);

	ClearInput();
	internal->has_input = false;
	internal->Schedule();
}

void
ThreadIstream::OnError(std::exception_ptr error) noexcept
{
	assert(internal);

	ClearInput();
	DestroyError(std::move(error));
}

UnusedIstreamPtr
NewThreadIstream(struct pool &pool, ThreadQueue &queue,
		 UnusedIstreamPtr input,
		 std::unique_ptr<ThreadIstreamFilter> filter) noexcept
{
	return NewIstreamPtr<ThreadIstream>(pool, queue,
					    std::move(input), std::move(filter));
}
