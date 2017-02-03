/*
 * Copyright (C) 2008-2016 Max Kellermann <max@duempel.org>
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

#include "Global.hxx"
#include "Request.hxx"
#include "event/SocketEvent.hxx"
#include "util/RuntimeError.hxx"

#include <curl/curl.h>

#include <string>

#include <assert.h>
#include <string.h>

/**
 * Monitor for one socket created by CURL.
 */
class CurlSocket final {
	CurlGlobal &global;

	const curl_socket_t fd;

	SocketEvent socket_event;

public:
	CurlSocket(CurlGlobal &_global, EventLoop &_loop, curl_socket_t _fd)
		:global(_global), fd(_fd),
		 socket_event(_loop, fd, 0, BIND_THIS_METHOD(OnSocketReady)) {}

	~CurlSocket() {
		socket_event.Delete();

		/* TODO: sometimes, CURL uses CURL_POLL_REMOVE after
		   closing the socket, and sometimes, it uses
		   CURL_POLL_REMOVE just to move the (still open)
		   connection to the pool; in the first case,
		   Abandon() would be most appropriate, but it breaks
		   the second case - is that a CURL bug?  is there a
		   better solution? */
	}

	void Schedule(unsigned events) {
		socket_event.Delete();
		socket_event.Set(fd, events|EV_PERSIST);
		socket_event.Add();
	}

	/**
	 * Callback function for CURLMOPT_SOCKETFUNCTION.
	 */
	static int SocketFunction(CURL *easy,
				  curl_socket_t s, int action,
				  void *userp, void *socketp);

private:
	void OnSocketReady(unsigned events);

	static constexpr int LibEventToCurlCSelect(unsigned flags) {
		return (flags & EV_READ ? CURL_CSELECT_IN : 0) |
			(flags & EV_WRITE ? CURL_CSELECT_OUT : 0);
	}

	gcc_const
	static unsigned CurlPollToLibEvent(int action) {
		switch (action) {
		case CURL_POLL_NONE:
			return 0;

		case CURL_POLL_IN:
			return EV_READ;

		case CURL_POLL_OUT:
			return EV_WRITE;

		case CURL_POLL_INOUT:
			return EV_READ|EV_WRITE;
		}

		assert(false);
		gcc_unreachable();
	}
};

CurlGlobal::CurlGlobal(EventLoop &_loop)
	:event_loop(_loop),
	 read_info_event(_loop, BIND_THIS_METHOD(OnDeferredReadInfo)),
	 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout))
{
	multi.SetOption(CURLMOPT_SOCKETFUNCTION, CurlSocket::SocketFunction);
	multi.SetOption(CURLMOPT_SOCKETDATA, this);

	multi.SetOption(CURLMOPT_TIMERFUNCTION, TimerFunction);
	multi.SetOption(CURLMOPT_TIMERDATA, this);
}

int
CurlSocket::SocketFunction(gcc_unused CURL *easy,
			   curl_socket_t s, int action,
			   void *userp, void *socketp) {
	auto &global = *(CurlGlobal *)userp;
	auto *cs = (CurlSocket *)socketp;

	if (action == CURL_POLL_REMOVE) {
		delete cs;
		return 0;
	}

	if (cs == nullptr) {
		cs = new CurlSocket(global, global.GetEventLoop(), s);
		global.Assign(s, *cs);
	}

	unsigned flags = CurlPollToLibEvent(action);
	if (flags != 0)
		cs->Schedule(flags);
	return 0;
}

void
CurlSocket::OnSocketReady(unsigned events)
{
	global.SocketAction(fd, LibEventToCurlCSelect(events));
}

void
CurlGlobal::Add(CurlRequest &r)
{
	CURLMcode mcode = curl_multi_add_handle(multi.Get(), r.Get());
	if (mcode != CURLM_OK)
		throw FormatRuntimeError("curl_multi_add_handle() failed: %s",
					 curl_multi_strerror(mcode));

	InvalidateSockets();
}

void
CurlGlobal::Remove(CurlRequest &r)
{
	curl_multi_remove_handle(multi.Get(), r.Get());
	InvalidateSockets();
}

/**
 * Find a request by its CURL "easy" handle.
 */
gcc_pure
static CurlRequest *
ToRequest(CURL *easy)
{
	void *p;
	CURLcode code = curl_easy_getinfo(easy, CURLINFO_PRIVATE, &p);
	if (code != CURLE_OK)
		return nullptr;

	return (CurlRequest *)p;
}

static void
Done(CURL *handle, CURLcode result)
{
	auto *r = ToRequest(handle);
	assert(r != nullptr);

	r->Done(result);
}

inline void
CurlGlobal::ReadInfo()
{
	CURLMsg *msg;
	int msgs_in_queue;

	while ((msg = curl_multi_info_read(multi.Get(),
					   &msgs_in_queue)) != nullptr) {
		if (msg->msg == CURLMSG_DONE)
			Done(msg->easy_handle, msg->data.result);
	}
}

void
CurlGlobal::SocketAction(curl_socket_t fd, int ev_bitmask)
{
	int running_handles;
	CURLMcode mcode = curl_multi_socket_action(multi.Get(), fd, ev_bitmask,
						   &running_handles);
	(void)mcode;

	read_info_event.Schedule();
}

void
CurlGlobal::OnDeferredReadInfo()
{
	ReadInfo();
}

inline void
CurlGlobal::ScheduleTimeout(long timeout_ms)
{
	if (timeout_ms < 0) {
		timeout_event.Cancel();
		return;
	}

	if (timeout_ms < 10)
		/* CURL 7.21.1 likes to report "timeout=0", which
		   means we're running in a busy loop.  Quite a bad
		   idea to waste so much CPU.  Let's use a lower limit
		   of 10ms. */
		timeout_ms = 10;

	const struct timeval tv = {
		.tv_sec = timeout_ms / 1000,
		.tv_usec = (timeout_ms % 1000) * 1000,
	};

	timeout_event.Add(tv);
}

int
CurlGlobal::TimerFunction(gcc_unused CURLM *_multi, long timeout_ms, void *userp)
{
	auto &global = *(CurlGlobal *)userp;
	assert(_multi == global.multi.Get());
	global.ScheduleTimeout(timeout_ms);
	return 0;
}

void
CurlGlobal::OnTimeout()
{
	SocketAction(CURL_SOCKET_TIMEOUT, 0);
}
