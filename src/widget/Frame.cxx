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

#include "Frame.hxx"
#include "Request.hxx"
#include "Error.hxx"
#include "Widget.hxx"
#include "Context.hxx"
#include "LookupHandler.hxx"
#include "HttpResponseHandler.hxx"
#include "pool/SharedPtr.hxx"
#include "bp/session/Session.hxx"
#include "util/StringFormat.hxx"

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

	if (!widget.CheckApproval())
		throw WidgetError(*widget.parent, WidgetErrorCode::FORBIDDEN,
				  StringFormat<256>("widget '%s' is not allowed to embed widget '%s'",
						    widget.parent->GetLogName(),
						    widget.GetLogName()));

	try {
		widget.CheckHost(ctx->untrusted_host, ctx->site_name);
	} catch (...) {
		std::throw_with_nested(WidgetError(widget, WidgetErrorCode::FORBIDDEN,
						   "Untrusted host"));
	}

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

		if (!widget.CheckApproval()) {
			char msg[256];
			snprintf(msg, sizeof(msg),
				 "widget '%s' is not allowed to embed widget '%s'",
				 widget.parent->GetLogName(),
				 widget.GetLogName());
			throw WidgetError(WidgetErrorCode::FORBIDDEN, msg);
		}
	} catch (...) {
		widget.Cancel();
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
