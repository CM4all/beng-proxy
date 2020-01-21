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

#define HAVE_EXPECT_100
#define HAVE_CHUNKED_REQUEST_BODY
#define ENABLE_CLOSE_IGNORED_REQUEST_BODY
#define ENABLE_HUGE_BODY
#define USE_BUCKETS

#include "t_client.hxx"
#include "http_client.hxx"
#include "http/Headers.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "net/SocketDescriptor.hxx"
#include "fs/FilteredSocket.hxx"
#include "fb_pool.hxx"
#include "istream/UnusedPtr.hxx"
#include "stopwatch.hxx"

#include <sys/socket.h>
#include <sys/wait.h>

struct Connection {
	EventLoop &event_loop;
	const pid_t pid;
	FilteredSocket socket;

	Connection(EventLoop &_event_loop, pid_t _pid, SocketDescriptor fd)
		:event_loop(_event_loop), pid(_pid), socket(_event_loop) {
		socket.InitDummy(fd, FdType::FD_SOCKET);
	}

	static Connection *New(EventLoop &event_loop,
			       const char *path, const char *mode);

	~Connection();

	void Request(struct pool *pool,
		     Lease &lease,
		     http_method_t method, const char *uri,
		     StringMap &&headers,
		     UnusedIstreamPtr body,
		     bool expect_100,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) {
		http_client_request(*pool, nullptr,
				    socket, lease,
				    "localhost",
				    method, uri, HttpHeaders(std::move(headers)),
				    std::move(body), expect_100,
				    handler, cancel_ptr);
	}

	void InjectSocketFailure() noexcept {
		socket.Shutdown();
	}

	static Connection *NewMirror(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "mirror");
	}

	static Connection *NewNull(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "null");
	}

	static Connection *NewDummy(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "dummy");
	}

	static Connection *NewClose(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "close");
	}

	static Connection *NewFixed(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "fixed");
	}

	static Connection *NewTiny(struct pool &p, EventLoop &event_loop) {
		return NewFixed(p, event_loop);
	}

	static Connection *NewHuge(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "huge");
	}

	static Connection *NewTwice100(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/twice_100.sh", nullptr);
	}

	static Connection *NewClose100(struct pool &, EventLoop &event_loop);

	static Connection *NewHold(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "hold");
	}

	static auto *NewNop(struct pool &, EventLoop &event_loop) {
		return New(event_loop, "./test/run_http_server", "nop");
	}
};

Connection::~Connection()
{
	assert(pid >= 1);

	socket.Close();
	socket.Destroy();

	int status;
	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid() failed");
		exit(EXIT_FAILURE);
	}

	assert(!WIFSIGNALED(status));
}

Connection *
Connection::New(EventLoop &event_loop, const char *path, const char *mode)
{
	SocketDescriptor client_socket, server_socket;
	if (!SocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						client_socket, server_socket)) {
		perror("socketpair() failed");
		exit(EXIT_FAILURE);
	}

	const auto pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		server_socket.CheckDuplicate(FileDescriptor(STDIN_FILENO));
		server_socket.CheckDuplicate(FileDescriptor(STDOUT_FILENO));

		execl(path, path,
		      "0", "0", mode, nullptr);

		const char *srcdir = getenv("srcdir");
		if (srcdir != nullptr) {
			/* support automake out-of-tree build */
			if (chdir(srcdir) == 0)
				execl(path, path,
				      "0", "0", mode, nullptr);
		}

		perror("exec() failed");
		_exit(EXIT_FAILURE);
	}

	server_socket.Close();
	client_socket.SetNonBlocking();
	return new Connection(event_loop, pid, client_socket);
}

Connection *
Connection::NewClose100(struct pool &, EventLoop &event_loop)
{
	SocketDescriptor client_socket, server_socket;
	if (!SocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						client_socket, server_socket)) {
		perror("socketpair() failed");
		exit(EXIT_FAILURE);
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		client_socket.Close();

		static const char response[] = "HTTP/1.1 100 Continue\n\n";
		(void)server_socket.Write(response, sizeof(response) - 1);
		server_socket.ShutdownWrite();

		char buffer[64];
		while (server_socket.Read(buffer, sizeof(buffer)) > 0) {}

		_exit(EXIT_SUCCESS);
	}

	server_socket.Close();
	client_socket.SetNonBlocking();
	return new Connection(event_loop, pid, client_socket);
}

/**
 * Keep-alive disabled, and response body has unknown length, ends
 * when server closes socket.  Check if our HTTP client handles such
 * responses correctly.
 */
template<class Connection>
static void
test_no_keepalive(Context<Connection> &c)
{
	c.connection = Connection::NewClose(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HTTP_METHOD_GET, "/foo", {},
			      nullptr,
#ifdef HAVE_EXPECT_100
			      false,
#endif
			      c, c.cancel_ptr);
	pool_commit();

	c.WaitForResponse();

	assert(c.status == HTTP_STATUS_OK);
	assert(c.request_error == nullptr);

	/* receive the rest of the response body from the buffer */
	c.event_loop.Dispatch();

	assert(c.released);
	assert(c.body_eof);
	assert(c.body_data > 0);
	assert(c.body_error == nullptr);
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

	run_all_tests<Connection>();
	run_test<Connection>(test_no_keepalive);
}
