// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ApproveThreadSocketFilter.hxx"
#include "util/ScopeExit.hxx"

void
ApproveThreadSocketFilter::Approve(std::size_t nbytes) noexcept
{
	approved += nbytes;

	bool _busy;

	{
		const std::scoped_lock lock{mutex};
		_busy = busy;
		if (_busy)
			cond.notify_one();
	}

	if (!_busy)
		ScheduleRun();
}

std::size_t
ApproveThreadSocketFilter::WaitForApproval() noexcept
{
	std::unique_lock lock{mutex};
	cond.wait(lock, [this]{ return approved > 0 || cancel; });
	busy = false;
	return approved;
}

void
ApproveThreadSocketFilter::PreRun(ThreadSocketFilterInternal &) noexcept
{
	std::unique_lock lock{mutex};
	busy = true;
}

void
ApproveThreadSocketFilter::Run(ThreadSocketFilterInternal &f)
{
	AtScopeExit(this) {
		const std::scoped_lock lock{mutex};
		busy = false;
	};

	{
		const std::scoped_lock lock{f.mutex};
		f.handshaking = false;
		f.encrypted_output.MoveFromAllowBothNull(f.plain_output);

		/* move to our own buffer, even if that content has not yet
		   been approved; this simulates the semantics of class
		   SslFilter */
		input.MoveFromAllowBothNull(f.encrypted_input);
		if (!f.encrypted_input.empty())
			f.again = true;
		f.drained = input.empty();
	}

	if (!input.empty()) {
		const std::size_t _approved = WaitForApproval();

		const std::scoped_lock lock{f.mutex};

		if (f.decrypted_input.IsNull()) {
			/* retry, let PreRun() allocate the missing buffer */
			f.again = true;
			return;
		}

		auto r = input.Read();
		auto w = f.decrypted_input.Write();

		std::size_t n = std::min(r.size(), w.size());
		if (n > _approved) {
			n = _approved;
			f.again = true;
		}

		std::copy_n(r.begin(), n, w.begin());
		input.Consume(n);
		f.decrypted_input.Append(n);
		f.drained = input.empty();

		approved -= n;
	}
}

void
ApproveThreadSocketFilter::CancelRun(ThreadSocketFilterInternal &) noexcept
{
	const std::scoped_lock lock{mutex};
	cancel = true;
	cond.notify_one();
}
