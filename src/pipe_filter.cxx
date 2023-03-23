// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "pipe_filter.hxx"
#include "http/ResponseHandler.hxx"
#include "stopwatch.hxx"
#include "istream_stopwatch.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"
#include "istream/UnusedPtr.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/IstreamSpawn.hxx"
#include "spawn/Prepared.hxx"
#include "util/HexFormat.hxx"
#include "util/djbhash.h"

#include <stdio.h>
#include <string.h>

static const char *
append_etag(AllocatorPtr alloc, const char *in, const char *suffix)
{
	size_t length;

	if (*in != '"')
		/* simple concatenation */
		return alloc.Concat(in, suffix);

	length = strlen(in + 1);
	if (in[length] != '"')
		return alloc.Concat(in, suffix);

	return alloc.Concat(std::string_view{in, length},
			    suffix,
			    '"');
}

template<typename A, typename E>
static const char *
make_pipe_etag(AllocatorPtr alloc, const char *in,
	       const char *path,
	       const A &args,
	       const E &env)
{
	char suffix[10] = {'-'};

	/* build hash from path and arguments */
	unsigned hash = djb_hash_string(path);

	for (auto i : args)
		hash ^= djb_hash_string(i);

	for (auto i : env)
		hash ^= djb_hash_string(i);

	*HexFormatUint32Fixed(suffix + 1, hash) = 0;

	/* append the hash to the old ETag */
	return append_etag(alloc, in, suffix);
}

void
pipe_filter(SpawnService &spawn_service, EventLoop &event_loop,
	    struct pool &pool,
	    const StopwatchPtr &parent_stopwatch,
	    const char *path,
	    std::span<const char *const> args,
	    const ChildOptions &options,
	    HttpStatus status, StringMap &&headers, UnusedIstreamPtr body,
	    HttpResponseHandler &handler)
{
	const char *etag;

	if (!body) {
		/* if the resource does not have a body (which is different
		   from Content-Length:0), don't filter it */
		handler.InvokeResponse(status, std::move(headers), UnusedIstreamPtr());
		return;
	}

	assert(!http_status_is_empty(status));

	StopwatchPtr stopwatch(parent_stopwatch, path);

	PreparedChildProcess p;
	p.Append(path);
	for (auto i : args)
		p.Append(i);

	UnusedIstreamPtr response;

	try {
		options.CopyTo(p);
		response = SpawnChildProcess(event_loop, &pool, path, std::move(body),
					     std::move(p),
					     spawn_service);
	} catch (...) {
		handler.InvokeError(std::current_exception());
		return;
	}

	stopwatch.RecordEvent("fork");

	etag = headers.Remove("etag");
	if (etag != nullptr) {
		/* we cannot pass the original ETag to the client, because the
		   pipe has modified the resource (which is what the pipe is
		   all about) - append a digest value to the ETag, which is
		   built from the program path and its arguments */

		etag = make_pipe_etag(pool, etag, path, args,
				      options.env);
		assert(etag != nullptr);

		headers.Add(pool, "etag", etag);
	}

	/* contents change, digest changes: discard the header if it
	   exists */
	headers.Remove("digest");

	response = istream_stopwatch_new(pool, std::move(response),
					 std::move(stopwatch));

	handler.InvokeResponse(status, std::move(headers), std::move(response));
}
