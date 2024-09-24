// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SRequest.hxx"
#include "SConnection.hxx"
#include "SLease.hxx"
#include "Client.hxx"
#include "pool/pool.hxx"
#include "stock/Item.hxx"
#include "stock/Stock.hxx"

void
WasStockRequest::OnStockItemReady(StockItem &item) noexcept
{
	auto &connection = static_cast<WasStockConnection &>(item);
	connection.SetSite(site_name);
	connection.SetUri(pending_request.uri);

	const auto &process = connection.GetSocket();
	auto &lease = *NewFromPool<WasStockLease>(pool, connection);

	was_client_request(pool, item.GetStock().GetEventLoop(),
			   std::move(stopwatch),
			   process.control,
			   process.input, process.output,
			   lease,
			   remote_host,
			   pending_request.method, pending_request.uri,
			   script_name, path_info,
			   query_string,
			   pending_request.headers,
			   std::move(pending_request.body),
			   parameters,
			   metrics_handler,
			   *this, cancel_ptr);
}

void
WasStockRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::move(ep));
}

void
WasStockRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
				UnusedIstreamPtr _body) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
WasStockRequest::OnHttpError(std::exception_ptr error) noexcept
{
	if (retries > 0 && IsWasClientRetryFailure(error)) {
		/* the server has closed the connection prematurely, maybe
		   because it didn't want to get any further requests on that
		   WAS connection.  Let's try again. */

		--retries;
		GetStockItem();
		return;
	}

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::move(error));
}

void
WasStockRequest::Cancel() noexcept
{
	auto c = std::move(cancel_ptr);
	Destroy();
	c.Cancel();
}
