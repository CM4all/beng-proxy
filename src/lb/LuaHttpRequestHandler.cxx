// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "HttpConnection.hxx"
#include "LuaHandler.hxx"
#include "LuaRequest.hxx"
#include "lua/CoRunner.hxx"
#include "lua/Resume.hxx"
#include "lua/Ref.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

using std::string_view_literals::operator""sv;

class LbLuaResponseHandler final
	: public HttpResponseHandler, Lua::ResumeListener,
	  Cancellable
{
	LbHttpConnection &connection;

	IncomingHttpRequest &request;

	/**
	 * This object temporarily holds the request body
	 */
	UnusedHoldIstreamPtr request_body;

	CancellablePointer &caller_cancel_ptr;

	[[no_unique_address]]
	const StopwatchPtr stopwatch;

	LbLuaHandler &handler;

	/**
	 * The Lua thread which runs the handler coroutine.
	 */
	Lua::CoRunner thread;

	Lua::Ref lua_request_ref;

	LbLuaRequestData *lua_request = nullptr;

	bool finished = false;

public:
	LbLuaResponseHandler(LbHttpConnection &_connection,
			     IncomingHttpRequest &_request,
			     CancellablePointer &_caller_cancel_ptr,
			     const StopwatchPtr &parent_stopwatch,
			     LbLuaHandler &_handler) noexcept
		:connection(_connection), request(_request),
		 request_body(request.pool, std::move(request.body)),
		 caller_cancel_ptr(_caller_cancel_ptr),
		 stopwatch(parent_stopwatch, "lua"),
		 handler(_handler),
		 thread(handler.GetMainState())
	{
		caller_cancel_ptr = *this;
	}

	~LbLuaResponseHandler() noexcept {
		if (lua_request != nullptr)
			lua_request->stale = true;
	}

	bool IsFinished() const noexcept {
		return finished;
	}

	void Start();

private:
	void Destroy() noexcept {
		this->~LbLuaResponseHandler();
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lua::ResumeListener */
	void OnLuaFinished(lua_State *L) noexcept override;
	void OnLuaError(lua_State *L, std::exception_ptr e) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;
};

inline void
LbLuaResponseHandler::Start()
{
	assert(lua_request == nullptr);

	lua_State *L = thread.CreateThread(*this);

	handler.PushFunction(L);

	lua_request = NewLuaRequest(L, connection, request, *this);
	lua_request_ref = {L, Lua::RelativeStackIndex{-1}};

	Lua::Resume(L, 1);
}

void
LbLuaResponseHandler::OnHttpResponse(HttpStatus status,
				     StringMap &&_headers,
				     UnusedIstreamPtr response_body) noexcept
{
	finished = true;
	request_body.Clear();

	HttpHeaders headers(std::move(_headers));

	if (request.method == HttpMethod::HEAD && !connection.IsHTTP2())
		/* pass Content-Length, even though there is no response body
		   (RFC 2616 14.13) */
		headers.MoveToBuffer(content_length_header);

	request.SendResponse(status, std::move(headers), std::move(response_body));
}

void
LbLuaResponseHandler::OnHttpError(std::exception_ptr ep) noexcept
{
	finished = true;
	request_body.Clear();

	connection.LogSendError(request, ep);
}

void
LbLuaResponseHandler::OnLuaFinished(lua_State *L) noexcept
try {
	const LbGoto *g = handler.Finish(L, request.pool);

	if (IsFinished()) {
		Destroy();
		return;
	}

	if (g == nullptr) {
		auto &_request = request;
		Destroy();

		_request.body.Clear();
		_request.SendMessage(HttpStatus::BAD_GATEWAY,
				     "No response from Lua handler"sv);
		return;
	}

	request.body = std::move(request_body);

	auto &_request = request;
	auto &cancel_ptr = caller_cancel_ptr;
	auto _stopwatch = std::move(stopwatch);
	Destroy();

	connection.HandleHttpRequest(*g, _request, _stopwatch, cancel_ptr);
} catch (...) {
	OnLuaError(L, std::current_exception());
}

void
LbLuaResponseHandler::OnLuaError(lua_State *, std::exception_ptr e) noexcept
{
	if (IsFinished())
		connection.logger(1, "Lua error: ", e);
	else
		InvokeError(std::move(e));
	Destroy();
}

void
LbLuaResponseHandler::Cancel() noexcept
{
	thread.Cancel();
	Destroy();
}

void
LbHttpConnection::InvokeLua(LbLuaHandler &handler,
			    IncomingHttpRequest &request,
			    const StopwatchPtr &parent_stopwatch,
			    CancellablePointer &cancel_ptr) noexcept
{
	auto *response_handler = NewFromPool<LbLuaResponseHandler>(request.pool, *this,
								   request, cancel_ptr,
								   parent_stopwatch,
								   handler);
	response_handler->Start();
}
