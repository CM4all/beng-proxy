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

#include "FailingResourceLoader.hxx"
#include "bp/XmlProcessor.hxx"
#include "bp/WidgetLookupProcessor.hxx"
#include "PInstance.hxx"
#include "widget/Inline.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "widget/LookupHandler.hxx"
#include "widget/RewriteUri.hxx"
#include "istream/istream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/istream_string.hxx"
#include "pool/pool.hxx"
#include "pool/SharedPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/*
 * emulate missing libraries
 *
 */

UnusedIstreamPtr
embed_inline_widget(struct pool &pool,
		    SharedPoolPtr<WidgetContext>,
		    const StopwatchPtr &,
		    gcc_unused bool plain_text,
		    Widget &widget) noexcept
{
	const char *s = widget.GetIdPath();
	if (s == nullptr)
		s = "widget";

	return istream_string_new(pool, s);
}

WidgetSession *
Widget::GetSession(gcc_unused RealmSession &session,
		   gcc_unused bool create) noexcept
{
	return nullptr;
}

RewriteUriMode
parse_uri_mode(gcc_unused StringView s) noexcept
{
	return RewriteUriMode::DIRECT;
}

UnusedIstreamPtr
rewrite_widget_uri(gcc_unused struct pool &pool,
		   SharedPoolPtr<WidgetContext> , const StopwatchPtr &,
		   gcc_unused Widget &widget,
		   gcc_unused StringView value,
		   gcc_unused RewriteUriMode mode,
		   gcc_unused bool stateful,
		   gcc_unused const char *view,
		   gcc_unused const struct escape_class *escape) noexcept
{
	return nullptr;
}

/*
 * WidgetLookupHandler
 *
 */

class MyWidgetLookupHandler final : public WidgetLookupHandler {
public:
	/* virtual methods from class WidgetLookupHandler */
	void WidgetFound(gcc_unused Widget &widget) noexcept override {
		fprintf(stderr, "widget found\n");
	}

	void WidgetNotFound() noexcept override {
		fprintf(stderr, "widget not found\n");
	}

	void WidgetLookupError(std::exception_ptr ep) noexcept override {
		PrintException(ep);
	}
};

/*
 * tests
 *
 */

TEST(Processor, Abort)
{
	PInstance instance;

	auto pool = pool_new_libc(instance.root_pool, "test");

	SessionId session_id;
	session_id.Generate();

	FailingResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(*pool, instance.event_loop,
		 resource_loader, resource_loader,
		 nullptr,
		 nullptr, nullptr,
		 "localhost:8080",
		 "localhost:8080",
		 "/beng.html",
		 "http://localhost:8080/beng.html",
		 "/beng.html",
		 nullptr,
		 "bp_session", session_id, "foo",
		 nullptr);
	auto &widget = ctx->AddRootWidget(MakeRootWidget(instance.root_pool,
							 nullptr));

	CancellablePointer cancel_ptr;
	MyWidgetLookupHandler handler;
	processor_lookup_widget(*pool, nullptr, istream_block_new(*pool),
				widget, "foo", std::move(ctx),
				PROCESSOR_CONTAINER,
				handler, cancel_ptr);

	cancel_ptr.Cancel();

	pool.reset();
	pool_commit();
}
