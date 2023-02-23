// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HttpConnection.hxx"
#include "LuaHandler.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"

class LbLuaResponseHandler final : public HttpResponseHandler {
	LbHttpConnection &connection;

	IncomingHttpRequest &request;

	bool finished = false;

public:
	LbLuaResponseHandler(LbHttpConnection &_connection,
			     IncomingHttpRequest &_request) noexcept
		:connection(_connection), request(_request) {}

	bool IsFinished() const noexcept {
		return finished;
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

void
LbLuaResponseHandler::OnHttpResponse(HttpStatus status,
				     StringMap &&_headers,
				     UnusedIstreamPtr response_body) noexcept
{
	finished = true;

	HttpHeaders headers(std::move(_headers));

	if (request.method == HttpMethod::HEAD && !connection.IsHTTP2())
		/* pass Content-Length, even though there is no response body
		   (RFC 2616 14.13) */
		headers.MoveToBuffer("content-length");

	request.SendResponse(status, std::move(headers), std::move(response_body));
}

void
LbLuaResponseHandler::OnHttpError(std::exception_ptr ep) noexcept
{
	finished = true;

	connection.LogSendError(request, ep);
}

void
LbHttpConnection::InvokeLua(LbLuaHandler &handler,
			    IncomingHttpRequest &request,
			    const StopwatchPtr &parent_stopwatch,
			    CancellablePointer &cancel_ptr) noexcept
{
	LbLuaResponseHandler response_handler(*this, request);
	const LbGoto *g;

	try {
		g = handler.HandleRequest(request, response_handler);
	} catch (...) {
		if (response_handler.IsFinished())
			logger(1, "Lua error: ", std::current_exception());
		else
			response_handler.InvokeError(std::current_exception());
		return;
	}

	if (response_handler.IsFinished())
		return;

	if (g == nullptr) {
		request.body.Clear();
		request.SendMessage(HttpStatus::BAD_GATEWAY,
				    "No response from Lua handler");
		return;
	}

	HandleHttpRequest(*g, request, parent_stopwatch, cancel_ptr);
	return;
}
