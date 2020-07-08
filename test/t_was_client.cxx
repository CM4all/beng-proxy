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

#define HAVE_CHUNKED_REQUEST_BODY
#define ENABLE_HUGE_BODY
#define NO_EARLY_RELEASE_SOCKET // TODO: improve the WAS client

#include "t_client.hxx"
#include "tio.hxx"
#include "stopwatch.hxx"
#include "was/Client.hxx"
#include "was/Server.hxx"
#include "was/Lease.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "net/SocketDescriptor.hxx"
#include "lease.hxx"
#include "istream/UnusedPtr.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"

#include "util/Compiler.h"

#include <functional>

static void
RunNull(WasServer &server, struct pool &,
	gcc_unused http_method_t method,
	gcc_unused const char *uri, gcc_unused StringMap &&headers,
	UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HTTP_STATUS_NO_CONTENT, {}, nullptr);
}

static void
RunHello(WasServer &server, struct pool &pool,
	 gcc_unused http_method_t method,
	 gcc_unused const char *uri, gcc_unused StringMap &&headers,
	 UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HTTP_STATUS_OK, {},
			    istream_string_new(pool, "hello"));
}

static void
RunHuge(WasServer &server, struct pool &pool,
	gcc_unused http_method_t method,
	gcc_unused const char *uri, gcc_unused StringMap &&headers,
	UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HTTP_STATUS_OK, {},
			    istream_head_new(pool,
					     istream_zero_new(pool),
					     524288, true));
}

static void
RunHold(WasServer &server, struct pool &pool,
	gcc_unused http_method_t method,
	gcc_unused const char *uri, gcc_unused StringMap &&headers,
	UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HTTP_STATUS_OK, {},
			    istream_block_new(pool));
}

static void
RunNop(WasServer &, struct pool &,
       http_method_t ,
       const char *, StringMap &&,
       UnusedIstreamPtr) noexcept
{
}

static void
RunMirror(WasServer &server, gcc_unused struct pool &pool,
	  gcc_unused http_method_t method,
	  gcc_unused const char *uri, StringMap &&headers,
	  UnusedIstreamPtr body)
{
	const bool has_body = body;
	server.SendResponse(has_body ? HTTP_STATUS_OK : HTTP_STATUS_NO_CONTENT,
			    std::move(headers), std::move(body));
}

static void
RunMalformedHeaderName(WasServer &server, gcc_unused struct pool &pool,
		       http_method_t, const char *, StringMap &&,
		       UnusedIstreamPtr body)
{
	body.Clear();

	StringMap response_headers(pool, {{"header name", "foo"}});

	server.SendResponse(HTTP_STATUS_NO_CONTENT,
			    std::move(response_headers), nullptr);
}

static void
RunMalformedHeaderValue(WasServer &server, gcc_unused struct pool &pool,
			http_method_t, const char *, StringMap &&,
			UnusedIstreamPtr body)
{
	body.Clear();

	StringMap response_headers(pool, {{"name", "foo\nbar"}});

	server.SendResponse(HTTP_STATUS_NO_CONTENT,
			    std::move(response_headers), nullptr);
}

class WasConnection final : WasServerHandler, WasLease {
	EventLoop &event_loop;

	SocketDescriptor control_fd;
	FileDescriptor input_fd, output_fd;

	WasServer *server;

	Lease *lease;

	typedef std::function<void(WasServer &server, struct pool &pool,
				   http_method_t method,
				   const char *uri, StringMap &&headers,
				   UnusedIstreamPtr body)> Callback;

	const Callback callback;

public:
	WasConnection(struct pool &pool, EventLoop &_event_loop,
		      Callback &&_callback)
		:event_loop(_event_loop), callback(std::move(_callback)) {
		FileDescriptor input_w;
		if (!FileDescriptor::CreatePipeNonBlock(input_fd, input_w)) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}

		FileDescriptor output_r;
		if (!FileDescriptor::CreatePipeNonBlock(output_r, output_fd)) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}

		SocketDescriptor control_server;
		if (!SocketDescriptor::CreateSocketPairNonBlock(AF_LOCAL, SOCK_STREAM, 0,
								control_fd,
								control_server)) {
			perror("socketpair");
			exit(EXIT_FAILURE);
		}

		WasServerHandler &handler = *this;
		server = NewFromPool<WasServer>(pool, pool, event_loop,
						control_server,
						output_r, input_w, handler);
	}

	~WasConnection() {
		control_fd.Close();
		input_fd.Close();
		output_fd.Close();

		if (server != nullptr)
			server->Free();
	}

	void Request(struct pool *pool,
		     Lease &_lease,
		     http_method_t method, const char *uri,
		     StringMap &&headers, UnusedIstreamPtr body,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) {
		lease = &_lease;
		was_client_request(*pool, event_loop, nullptr,
				   control_fd, input_fd, output_fd,
				   *this,
				   method, uri, uri, nullptr, nullptr,
				   headers, std::move(body), nullptr,
				   handler, cancel_ptr);
	}

	void InjectSocketFailure() noexcept {
		control_fd.Shutdown();
	}

	/* virtual methods from class WasServerHandler */

	void OnWasRequest(struct pool &pool, http_method_t method,
			  const char *uri, StringMap &&headers,
			  UnusedIstreamPtr body) noexcept override {
		callback(*server, pool, method, uri,
			 std::move(headers), std::move(body));
	}

	void OnWasClosed() noexcept override {
		server = nullptr;
	}

	/* constructors */

	static WasConnection *NewMirror(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunMirror);
	}

	static WasConnection *NewNull(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunNull);
	}

	static WasConnection *NewDummy(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHello);
	}

	static WasConnection *NewFixed(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHello);
	}

	static WasConnection *NewTiny(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHello);
	}

	static WasConnection *NewHuge(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHuge);
	}

	static WasConnection *NewHold(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHold);
	}

	static WasConnection *NewNop(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunNop);
	}

	static WasConnection *NewMalformedHeaderName(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunMalformedHeaderName);
	}

	static WasConnection *NewMalformedHeaderValue(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunMalformedHeaderValue);
	}

private:
	/* virtual methods from class WasLease */
	void ReleaseWas(bool reuse) override {
		lease->ReleaseLease(reuse);
	}

	void ReleaseWasStop(gcc_unused uint64_t input_received) override {
		ReleaseWas(false);
	}
};

template<class Connection>
static void
test_malformed_header_name(Context<Connection> &c)
{
	c.connection = Connection::NewMalformedHeaderName(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.status == http_status_t(0));
	assert(c.request_error);
	assert(c.released);
}

template<class Connection>
static void
test_malformed_header_value(Context<Connection> &c)
{
	c.connection = Connection::NewMalformedHeaderValue(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);

	c.event_loop.Dispatch();

	assert(c.status == http_status_t(0));
	assert(c.request_error);
	assert(c.released);
}

/*
 * main
 *
 */

int
main(int, char **)
{
	SetupProcess();
	direct_global_init();
	const ScopeFbPoolInit fb_pool_init;

	run_all_tests<WasConnection>();
	run_test<WasConnection>(test_malformed_header_name);
	run_test<WasConnection>(test_malformed_header_value);
}
