// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "tconstruct.hxx"
#include "cgi/Glue.hxx"
#include "cgi/Address.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "spawn/Config.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Local.hxx"
#include "system/SetupProcess.hxx"
#include "system/KernelVersion.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static SpawnConfig spawn_config;

struct Context final : TestInstance, HttpResponseHandler, IstreamSink {
	ChildProcessRegistry child_process_registry;
	LocalSpawnService spawn_service;

	CancellablePointer cancel_ptr;

	unsigned data_blocking = 0;
	bool close_response_body_early = false;
	bool close_response_body_late = false;
	bool close_response_body_data = false;
	bool body_read = false, no_content = false;
	bool released = false, aborted = false;
	HttpStatus status = HttpStatus{};

	off_t body_data = 0, body_available = 0;
	bool body_eof = false, body_abort = false, body_closed = false;

	Context()
		:spawn_service(spawn_config, event_loop,
			       child_process_registry)
	{
	}

	using IstreamSink::HasInput;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

static FdTypeMask my_handler_direct = 0;

/*
 * istream handler
 *
 */

std::size_t
Context::OnData(std::span<const std::byte> src) noexcept
{
	body_data += src.size();

	if (close_response_body_data) {
		body_closed = true;
		CloseInput();
		return 0;
	}

	if (data_blocking) {
		--data_blocking;
		return 0;
	}

	return src.size();
}

IstreamDirectResult
Context::OnDirect(FdType, FileDescriptor fd, off_t offset,
		  std::size_t max_length,
		  [[maybe_unused]] bool then_eof) noexcept
{
	if (close_response_body_data) {
		body_closed = true;
		CloseInput();
		return IstreamDirectResult::CLOSED;
	}

	if (data_blocking) {
		--data_blocking;
		return IstreamDirectResult::BLOCKING;
	}

	std::byte buffer[256];
	std::span<std::byte> w{buffer};
	if (w.size() > max_length)
		w = w.first(max_length);

	ssize_t nbytes = HasOffset(offset)
		? fd.ReadAt(offset, w)
		: fd.Read(w);
	if (nbytes <= 0)
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	body_data += nbytes;
	input.ConsumeDirect(nbytes);
	return IstreamDirectResult::OK;
}

void
Context::OnEof() noexcept
{
	ClearInput();
	body_eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
	ClearInput();
	body_abort = true;
}

/*
 * http_response_handler
 *
 */

void
Context::OnHttpResponse(HttpStatus _status, StringMap &&,
			UnusedIstreamPtr _body) noexcept
{
	assert(!no_content || !_body);

	status = _status;

	if (close_response_body_early) {
		_body.Clear();
	} else if (_body) {
		SetInput(std::move(_body));
		input.SetDirect(my_handler_direct);
		body_available = input.GetAvailable(false);
	}

	if (close_response_body_late) {
		body_closed = true;
		CloseInput();
	}

	if (body_read) {
		assert(HasInput());
		input.Read();
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

[[gnu::pure]]
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

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::OK);
	assert(!c->HasInput());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_tiny(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "tiny.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("tiny.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::OK);
	assert(!c->HasInput());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_close_early(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	c->close_response_body_early = true;

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::OK);
	assert(!c->HasInput());
	assert(!c->body_eof);
	assert(!c->body_abort);
}

static void
test_close_late(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	c->close_response_body_late = true;

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::OK);
	assert(!c->HasInput());
	assert(!c->body_eof);
	assert(c->body_abort || c->body_closed);
}

static void
test_close_data(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "env.py");

	c->close_response_body_data = true;

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("env.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::OK);
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

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("cat.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::POST, &address,
		nullptr, {},
		UnusedIstreamPtr(OpenFileIstream(c->event_loop, *pool,
						 "build.ninja")),
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::OK);
	assert(!c->HasInput());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_status(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "status.sh");

	c->body_read = true;

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("status.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::CREATED);
	assert(!c->HasInput());
	assert(c->body_eof);
	assert(!c->body_abort);
}

static void
test_no_content(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "no_content.sh");

	c->no_content = true;

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("no_content.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->status == HttpStatus::NO_CONTENT);
	assert(!c->HasInput());
	assert(!c->body_eof);
	assert(!c->body_abort);
}

static void
test_no_length(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length0.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("length0.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->body_available == -1);
	assert(c->body_eof);
}

static void
test_length_ok(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length1.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("length1.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->body_available == 4);
	assert(c->body_eof);
}

static void
test_length_ok_large(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length5.sh");

	c->body_read = true;

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("length5.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->body_available == 8192);
	assert(c->body_eof);
}

static void
test_length_too_small(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length2.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("length2.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(c->aborted);
}

static void
test_length_too_big(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length3.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("length3.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(!c->aborted);
	assert(c->body_abort);
}

static void
test_length_too_small_late(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "length4.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("length4.sh")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

	assert(!c->aborted);
	assert(c->body_abort ||
	       /* this error cannot be caught in the "direct" mode,
		  because in that mode, the CGI client limits the
		  number of bytes which can be read */
	       my_handler_direct != 0);
}

/**
 * Test a response header that is too large for the buffer.
 */
static void
test_large_header(PoolPtr pool, Context *c)
{
	const AllocatorPtr alloc(pool);
	const char *path = GetCgiPath(alloc, "large_header.sh");

	const auto address = MakeCgiAddress(alloc, path, "/")
		.ScriptName("large_header.py")
		.DocumentRoot("/var/www");

	cgi_new(c->spawn_service, c->event_loop,
		pool, nullptr, HttpMethod::GET, &address,
		nullptr, {}, nullptr,
		*c, c->cancel_ptr);

	pool.reset();
	pool_commit();

	c->event_loop.Run();

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

	// skip this unit test on old kernels without clone3()
	if (!IsKernelVersionOrNewer({5, 3}))
		return 77;

	SetupProcess();

	run_all_tests();

	my_handler_direct = FD_ANY;
	run_all_tests();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
