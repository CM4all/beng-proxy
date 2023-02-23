// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Frame.hxx"
#include "Request.hxx"
#include "Error.hxx"
#include "Widget.hxx"
#include "Context.hxx"
#include "LookupHandler.hxx"
#include "http/ResponseHandler.hxx"
#include "pool/SharedPtr.hxx"
#include "bp/session/Lease.hxx"

#include <assert.h>

void
frame_top_widget(struct pool &pool, Widget &widget,
		 SharedPoolPtr<WidgetContext> ctx,
		 const StopwatchPtr &parent_stopwatch,
		 HttpResponseHandler &handler,
		 CancellablePointer &cancel_ptr)
try {
	assert(widget.cls != nullptr);
	assert(widget.HasDefaultView());
	assert(widget.from_request.frame);

	widget.CheckApproval();
	widget.CheckHost(ctx->untrusted_host, ctx->site_name);

	if (widget.session_sync_pending) {
		auto session = ctx->GetRealmSession();
		if (session)
			widget.LoadFromSession(*session);
		else
			widget.session_sync_pending = false;
	}

	widget_http_request(pool, widget, std::move(ctx), parent_stopwatch,
			    handler, cancel_ptr);
} catch (...) {
	widget.Cancel();
	ctx.reset();
	handler.InvokeError(std::current_exception());
}

void
frame_parent_widget(struct pool &pool, Widget &widget, const char *id,
		    SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    WidgetLookupHandler &handler,
		    CancellablePointer &cancel_ptr)
{
	assert(widget.cls != nullptr);
	assert(widget.HasDefaultView());
	assert(!widget.from_request.frame);
	assert(id != nullptr);

	try {
		if (!widget.IsContainer()) {
			/* this widget cannot possibly be the parent of a framed
			   widget if it is not a container */
			throw WidgetError(WidgetErrorCode::NOT_A_CONTAINER,
					  "frame within non-container requested");
		}

		widget.CheckApproval();
	} catch (...) {
		widget.Cancel();
		ctx.reset();
		handler.WidgetLookupError(std::current_exception());
		return;
	}

	if (widget.session_sync_pending) {
		auto session = ctx->GetRealmSession();
		if (session)
			widget.LoadFromSession(*session);
		else
			widget.session_sync_pending = false;
	}

	widget_http_lookup(pool, widget, id, std::move(ctx), parent_stopwatch,
			   handler, cancel_ptr);
}
