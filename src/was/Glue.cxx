/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Glue.hxx"
#include "Stock.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "http/ResponseHandler.hxx"
#include "Lease.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "pool/LeakDetector.hxx"
#include "stopwatch.hxx"
#include "util/Cancellable.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringCompare.hxx"
#include "strmap.hxx"

#include <assert.h>
#include <string.h>

class WasRequest final : StockGetHandler, Cancellable, WasLease, PoolLeakDetector {
	struct pool &pool;

	StopwatchPtr stopwatch;

	const char *const site_name;

	StockItem *stock_item;

	http_method_t method;
	const char *uri;
	const char *script_name;
	const char *path_info;
	const char *query_string;
	const StringMap headers;
	UnusedHoldIstreamPtr body;

	ConstBuffer<const char *> parameters;

	HttpResponseHandler &handler;
	CancellablePointer &caller_cancel_ptr;
	CancellablePointer stock_cancel_ptr;

public:
	WasRequest(struct pool &_pool,
		   StopwatchPtr &&_stopwatch,
		   const char *_site_name,
		   http_method_t _method, const char *_uri,
		   const char *_script_name, const char *_path_info,
		   const char *_query_string,
		   StringMap &&_headers,
		   UnusedIstreamPtr _body,
		   ConstBuffer<const char *> _parameters,
		   HttpResponseHandler &_handler,
		   CancellablePointer &_cancel_ptr)
		:PoolLeakDetector(_pool),
		 pool(_pool),
		 stopwatch(std::move(_stopwatch)),
		 site_name(_site_name),
		 method(_method),
		 uri(_uri), script_name(_script_name),
		 path_info(_path_info), query_string(_query_string),
		 headers(std::move(_headers)),
		 body(pool, std::move(_body)),
		 parameters(_parameters),
		 handler(_handler), caller_cancel_ptr(_cancel_ptr) {
		caller_cancel_ptr = *this;
	}

	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	void Start(WasStock &was_stock, const ChildOptions &options,
		   const char *action, ConstBuffer<const char *> args) {
		was_stock.Get(pool,
			      options,
			      action, args,
			      *this, stock_cancel_ptr);
	}

private:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		auto c = std::move(stock_cancel_ptr);
		Destroy();
		c.Cancel();
	}

	/* virtual methods from class WasLease */
	void ReleaseWas(bool reuse) override {
		stock_item->Put(!reuse);
		Destroy();
	}

	void ReleaseWasStop(uint64_t input_received) override {
		was_stock_item_stop(*stock_item, input_received);
		stock_item->Put(false);
		Destroy();
	}
};

/*
 * stock callback
 *
 */

void
WasRequest::OnStockItemReady(StockItem &item) noexcept
{
	was_stock_item_set_site(item, site_name);
	was_stock_item_set_uri(item, uri);

	stock_item = &item;

	const auto &process = was_stock_item_get(item);

	was_client_request(pool, item.stock.GetEventLoop(), std::move(stopwatch),
			   process.control,
			   process.input, process.output,
			   *this,
			   method, uri,
			   script_name, path_info,
			   query_string,
			   headers, std::move(body),
			   parameters,
			   handler, caller_cancel_ptr);
}

void
WasRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

/*
 * constructor
 *
 */

#ifdef ENABLE_STOPWATCH

gcc_pure
static const char *
GetComaClass(ConstBuffer<const char *> parameters)
{
	for (const char *i : parameters) {
		const char *result = StringAfterPrefix(i, "COMA_CLASS=");
		if (result != nullptr && *result != 0)
			return result;
	}

	return nullptr;
}

#endif

static StopwatchPtr
stopwatch_new_was(const StopwatchPtr &parent_stopwatch,
		  const char *path, const char *uri,
		  const char *path_info,
		  ConstBuffer<const char *> parameters)
{
#ifdef ENABLE_STOPWATCH
	assert(path != nullptr);
	assert(uri != nullptr);

	if (!stopwatch_is_enabled())
		return nullptr;

	/* special case for a very common COMA application */
	const char *coma_class = GetComaClass(parameters);
	if (coma_class != nullptr)
		path = coma_class;

	const char *slash = strrchr(path, '/');
	if (slash != nullptr && slash[1] != 0)
		path = slash + 1;

	if (path_info != nullptr && *path_info != 0)
		uri = path_info;

	std::string name = path;
	name.push_back(' ');
	name += uri;

	return StopwatchPtr(parent_stopwatch, name.c_str());
#else
	(void)parent_stopwatch;
	(void)path;
	(void)uri;
	(void)path_info;
	(void)parameters;
	return nullptr;
#endif
}

void
was_request(struct pool &pool, WasStock &was_stock,
	    const StopwatchPtr &parent_stopwatch,
	    const char *site_name,
	    const ChildOptions &options,
	    const char *action,
	    const char *path,
	    ConstBuffer<const char *> args,
	    http_method_t method, const char *uri,
	    const char *script_name, const char *path_info,
	    const char *query_string,
	    StringMap &&headers, UnusedIstreamPtr body,
	    ConstBuffer<const char *> parameters,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr)
{
	if (action == nullptr)
		action = path;

	auto request = NewFromPool<WasRequest>(pool, pool,
					       stopwatch_new_was(parent_stopwatch,
								 path, uri,
								 path_info,
								 parameters),
					       site_name,
					       method, uri, script_name,
					       path_info, query_string,
					       std::move(headers),
					       std::move(body),
					       parameters,
					       handler, cancel_ptr);
	request->Start(was_stock, options, action, args);
}
