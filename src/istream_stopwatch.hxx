// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_ISTREAM_STOPWATCH_HXX
#define BENG_PROXY_ISTREAM_STOPWATCH_HXX

struct pool;
class UnusedIstreamPtr;
class StopwatchPtr;

#ifdef ENABLE_STOPWATCH

/**
 * This istream filter emits a stopwatch event and dump on eof/abort.
 */
UnusedIstreamPtr
istream_stopwatch_new(struct pool &pool, UnusedIstreamPtr input,
		      StopwatchPtr stopwatch);

#else /* !ENABLE_STOPWATCH */

static inline UnusedIstreamPtr &&
istream_stopwatch_new(struct pool &pool, UnusedIstreamPtr &&input,
		      StopwatchPtr &&_stopwatch)
{
	(void)pool;
	(void)_stopwatch;

	return std::move(input);
}

#endif /* !ENABLE_STOPWATCH */

#endif
