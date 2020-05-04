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

#include "tconstruct.hxx"
#include "cgi/Glue.hxx"
#include "cgi/Address.hxx"
#include "HttpResponseHandler.hxx"
#include "crash.hxx"
#include "strmap.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "PInstance.hxx"
#include "fb_pool.hxx"
#include "spawn/Config.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "system/SetupProcess.hxx"
#include "io/SpliceSupport.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include "util/Compiler.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static SpawnConfig spawn_config;

struct Context final : PInstance, HttpResponseHandler, IstreamHandler {
	ChildProcessRegistry child_process_registry;
	LocalSpawnService spawn_service;

	CancellablePointer cancel_ptr;

	unsigned data_blocking = 0;
	bool close_response_body_early = false;
	bool close_response_body_late = false;
	bool close_response_body_data = false;
	bool body_read = false, no_content = false;
	bool released = false, aborted = false;
	http_status_t status = http_status_t(0);

	IstreamPointer body;
	off_t body_data = 0, body_available = 0;
	bool body_eof = false, body_abort = false, body_closed = false;

	Context()
		:child_process_registry(event_loop),
		 spawn_service(spawn_config, child_process_registry),
		 body(nullptr) {
		child_process_registry.SetVolatile();
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class IstreamHandler */
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

static FdTypeMask my_handler_direct = 0;

/*
 * istream handler
 *
 */

size_t
Context::OnData(gcc_unused const void *data, size_t length) noexcept
{
	body_data += length;

	if (close_response_body_data) {
		body_closed = true;
		body.ClearAndClose();
		return 0;
	}

	if (data_blocking) {
		--data_blocking;
		return 0;
	}

	return length;
}

ssize_t
Context::OnDirect(gcc_unused FdType type, int fd, size_t max_length) noexcept
{
	if (close_response_body_data) {
		body_closed = true;
		body.ClearAndClose();
		return 0;
	}

	if (data_blocking) {
		--data_blocking;
		return ISTREAM_RESULT_BLOCKING;
	}

	char buffer[256];
	if (max_length > sizeof(buffer))
		max_length = sizeof(buffer);

	ssize_t nbytes = read(fd, buffer, max_length);
	if (nbytes <= 0)
		return nbytes;

	body_data += nbytes;
	return nbytes;
}

void
Context::OnEof() noexcept
{
	body.Clear();
	body_eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
	body.Clear();
	body_abort = true;
}

/*
 * http_response_handler
 *
 */

void
Context::OnHttpResponse(http_status_t _status, gcc_unused StringMap &&headers,
			UnusedIstreamPtr _body) noexcept
{
	assert(!no_content || !_body);

	status = _status;

	if (close_response_body_early) {
		_body.Clear();
	} else if (_body) {
		body.Set(std::move(_body), *this, my_handler_direct);
		body_available = body.GetAvailable(false);
	}

	if (close_response_body_late) {
		body_closed = true;
		body.ClearAndClose();
	}

	if (body_read) {
		assert(body.IsDefined());
		body.Read();
	}
}

void
Context::OnHttpError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	aborted = true;
}

/*
 * tests
 *
 */

gcc_pure
static const char *
GetCgiPath(AllocatorPtr alloc, const char *name) noexcept
{
	const char *srcdir = getenv("srcdir");
	if (srcdir == nullptr)
		srcdir = ".";

	return alloc.Concat(srcdir, "/demo/cgi-bin/", name);
}

static void
test_normal(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_OK);
	assert(!c->body.IsDefined());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_tiny(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "tiny.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("tiny.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_OK);
	assert(!c->body.IsDefined());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_close_early(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	c->close_response_body_early = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_OK);
	assert(!c->body.IsDefined());
	assert(!c->body_eof);
	assert(!c->body_abort);
}

static void
test_close_late(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	c->close_response_body_late = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_OK);
	assert(!c->body.IsDefined());
	assert(!c->body_eof);
	assert(c->body_abort || c->body_closed);
}

static void
test_close_data(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	c->close_response_body_data = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_OK);
	assert(!c->body_eof);
	assert(!c->body_abort);
	assert(c->body_closed);
}

static void
test_post(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "cat.sh");

	c->body_read = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("cat.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_POST, &address,
		nullptr, {},
		UnusedIstreamPtr(OpenFileIstream(c->event_loop, *pool,
						 "build.ninja")),
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_OK);
	assert(!c->body.IsDefined());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_status(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "status.sh");

	c->body_read = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("status.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_CREATED);
	assert(!c->body.IsDefined());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_no_content(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "no_content.sh");

	c->no_content = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("no_content.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->status == HTTP_STATUS_NO_CONTENT);
	assert(!c->body.IsDefined());
	assert(!c->body_eof);
	assert(!c->body_abort);
}

static void
test_no_length(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length0.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("length0.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->body_available == -1);
	assert(c->body_eof);
}

static void
test_length_ok(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length1.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("length1.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->body_available == 4);
	assert(c->body_eof);
}

static void
test_length_ok_large(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length5.sh");

	c->body_read = true;

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("length5.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->body_available == 8192);
	assert(c->body_eof);
}

static void
test_length_too_small(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length2.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("length2.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->aborted);
}

static void
test_length_too_big(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length3.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("length3.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(!c->aborted);
	assert(c->body_abort);
}

static void
test_length_too_small_late(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length4.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("length4.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(!c->aborted);
	assert(c->body_abort);
}

/**
 * Test a response header that is too large for the buffer.
 */
static void
test_large_header(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "large_header.sh");

	const auto address = MakeCgiAddress(pool, path, "/")
		.ScriptName("large_header.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HTTP_METHOD_GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Dispatch();

	assert(c->aborted);
	assert(!c->body_abort);
}


/*
 * main
 *
 */

static void
run_test(void (*test)(PoolPtr pool, Context *c))
{
	Context c;

	test(pool_new_linear(c.root_pool, "test", 16384), &c);
}

static void
run_all_tests()
{
	run_test(test_normal);
	run_test(test_tiny);
	run_test(test_close_early);
	run_test(test_close_late);
	run_test(test_close_data);
	run_test(test_post);
	run_test(test_status);
	run_test(test_no_content);
	run_test(test_no_length);
	run_test(test_length_ok);
	run_test(test_length_ok_large);
	run_test(test_length_too_small);
	run_test(test_length_too_big);
	run_test(test_length_too_small_late);
	run_test(test_large_header);
}

int
main(int argc, char **argv)
try {
	(void)argc;
	(void)argv;

	SetupProcess();

	direct_global_init();
	const ScopeCrashGlobalInit crash_init;
	const ScopeFbPoolInit fb_pool_init;

	run_all_tests();

	my_handler_direct = FD_ANY;
	run_all_tests();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
