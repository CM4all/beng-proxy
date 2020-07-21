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

#pragma once

struct pool;
class Widget;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class StopwatchPtr;
class HttpResponseHandler;
class CancellablePointer;
class WidgetLookupHandler;

/**
 * Sends a HTTP request to the widget, apply all transformations, and
 * return the result to the #http_response_handler.
 */
void
widget_http_request(struct pool &pool, Widget &widget,
		    SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;

/**
 * Send a HTTP request to the widget server, process it, and look up
 * the specified widget in the processed result.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
widget_http_lookup(struct pool &pool, Widget &widget, const char *id,
		   SharedPoolPtr<WidgetContext> ctx,
		   const StopwatchPtr &parent_stopwatch,
		   WidgetLookupHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;
