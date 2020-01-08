/*
 * Copyright 2007-2019 Content Management AG
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

#include "RewriteUri.hxx"
#include "Widget.hxx"
#include "Resolver.hxx"
#include "Class.hxx"
#include "Context.hxx"
#include "Inline.hxx"
#include "uri/Extract.hxx"
#include "pool/tpool.hxx"
#include "escape_class.hxx"
#include "istream_escape.hxx"
#include "istream/TimeoutIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "bp/session/Session.hxx"
#include "pool/pbuffer.hxx"
#include "util/StringView.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

RewriteUriMode
parse_uri_mode(const StringView s) noexcept
{
	if (s.Equals("direct"))
		return RewriteUriMode::DIRECT;
	else if (s.Equals("focus"))
		return RewriteUriMode::FOCUS;
	else if (s.Equals("partial"))
		return RewriteUriMode::PARTIAL;
	else if (s.Equals("response"))
		return RewriteUriMode::RESPONSE;
	else
		return RewriteUriMode::PARTIAL;
}

/*
 * The "real" rewriting code
 *
 */

static const char *
uri_replace_hostname(AllocatorPtr alloc, const char *uri,
		     const char *hostname) noexcept
{
	assert(hostname != nullptr);

	const auto old_host = uri_host_and_port(uri);
	if (old_host.IsNull())
		return *uri == '/'
			? alloc.Concat("//", hostname, uri)
			: nullptr;

	const char *colon = old_host.Find(':');
	const char *end = colon != nullptr ? colon : old_host.end();

	return alloc.Concat(StringView{uri, old_host.data},
			    hostname, end);
}

static const char *
uri_add_prefix(AllocatorPtr alloc, const char *uri, const char *absolute_uri,
	       const char *untrusted_host,
	       const char *untrusted_prefix) noexcept
{
	assert(untrusted_prefix != nullptr);

	if (untrusted_host != nullptr)
		/* this request comes from an untrusted host - either we're
		   already in the correct prefix (no-op), or this is a
		   different untrusted domain (not supported) */
		return uri;

	if (*uri == '/') {
		if (absolute_uri == nullptr)
			/* unknown old host name, we cannot do anything useful */
			return uri;

		const auto host = uri_host_and_port(absolute_uri);
		if (host.IsNull())
			return uri;

		return alloc.Concat(StringView{absolute_uri, host.data},
				    untrusted_prefix,
				    '.', host, uri);
	}

	const auto host = uri_host_and_port(uri);
	if (host.IsNull())
		return uri;

	return alloc.Concat(StringView{uri, host.data},
			    untrusted_prefix,
			    '.', host);
}

static const char *
uri_add_site_suffix(AllocatorPtr alloc, const char *uri, const char *site_name,
		    const char *untrusted_host,
		    const char *untrusted_site_suffix) noexcept
{
	assert(untrusted_site_suffix != nullptr);

	if (untrusted_host != nullptr)
		/* this request comes from an untrusted host - either we're
		   already in the correct suffix (no-op), or this is a
		   different untrusted domain (not supported) */
		return uri;

	if (site_name == nullptr)
		/* we don't know the site name of this request; we cannot do
		   anything, so we're just returning the unmodified URI, which
		   will render an error message */
		return uri;

	const char *path = uri_path(uri);
	if (path == nullptr)
		/* without an absolute path, we cannot build a new absolute
		   URI */
		return uri;

	return alloc.Concat("//", site_name, ".", untrusted_site_suffix, path);
}

static const char *
uri_add_raw_site_suffix(AllocatorPtr alloc, const char *uri, const char *site_name,
			const char *untrusted_host,
			const char *untrusted_raw_site_suffix) noexcept
{
	assert(untrusted_raw_site_suffix != nullptr);

	if (untrusted_host != nullptr)
		/* this request comes from an untrusted host - either we're
		   already in the correct suffix (no-op), or this is a
		   different untrusted domain (not supported) */
		return uri;

	if (site_name == nullptr)
		/* we don't know the site name of this request; we cannot do
		   anything, so we're just returning the unmodified URI, which
		   will render an error message */
		return uri;

	const char *path = uri_path(uri);
	if (path == nullptr)
		/* without an absolute path, we cannot build a new absolute
		   URI */
		return uri;

	return alloc.Concat("//", site_name, untrusted_raw_site_suffix, path);
}

/**
 * @return the new URI or nullptr if it is unchanged
 */
static const char *
do_rewrite_widget_uri(AllocatorPtr alloc, WidgetContext &ctx,
		      Widget &widget,
		      StringView value,
		      RewriteUriMode mode, bool stateful,
		      const char *view) noexcept
{
	if (widget.cls->local_uri != nullptr &&
	    value.size >= 2 && value[0] == '@' && value[1] == '/')
		/* relative to widget's "local URI" */
		return alloc.Concat(widget.cls->local_uri,
				    StringView{value.data + 2, value.size - 2});

	const char *frame = nullptr;

	switch (mode) {
	case RewriteUriMode::DIRECT:
		assert(widget.GetAddressView() != nullptr);
		if (!widget.GetAddressView()->address.IsHttp())
			/* the browser can only contact HTTP widgets directly */
			return nullptr;

		return widget.AbsoluteUri(alloc, stateful, value);

	case RewriteUriMode::FOCUS:
		frame = strmap_get_checked(ctx.args, "frame");
		break;

	case RewriteUriMode::PARTIAL:
		frame = widget.GetIdPath();

		if (frame == nullptr)
			/* no widget_path available - "frame=" not possible*/
			return nullptr;
		break;

	case RewriteUriMode::RESPONSE:
		assert(false);
		gcc_unreachable();
	}

	const char *uri = widget.ExternalUri(alloc, ctx.external_base_uri,
					     ctx.args,
					     stateful,
					     value,
					     frame, view);
	if (uri == nullptr) {
		if (widget.id == nullptr)
			widget.logger(4, "Cannot rewrite URI: no widget id");
		else if (widget.GetIdPath() == nullptr)
			widget.logger(4, "Cannot rewrite URI: broken widget id chain");
		else
			widget.logger(4, "Base mismatch: ", value);
		return nullptr;
	}

	if (widget.cls->untrusted_host != nullptr &&
	    (ctx.untrusted_host == nullptr ||
	     strcmp(widget.cls->untrusted_host, ctx.untrusted_host) != 0))
		uri = uri_replace_hostname(alloc, uri, widget.cls->untrusted_host);
	else if (widget.cls->untrusted_prefix != nullptr)
		uri = uri_add_prefix(alloc, uri, ctx.absolute_uri, ctx.untrusted_host,
				     widget.cls->untrusted_prefix);
	else if (widget.cls->untrusted_site_suffix != nullptr)
		uri = uri_add_site_suffix(alloc, uri, ctx.site_name,
					  ctx.untrusted_host,
					  widget.cls->untrusted_site_suffix);
	else if (widget.cls->untrusted_raw_site_suffix != nullptr)
		uri = uri_add_raw_site_suffix(alloc, uri, ctx.site_name,
					      ctx.untrusted_host,
					      widget.cls->untrusted_raw_site_suffix);

	return uri;
}


/*
 * widget_resolver callback
 *
 */

class UriRewriter {
	struct pool &pool;
	WidgetContext &ctx;
	Widget &widget;

	/** the value passed to rewrite_widget_uri() */
	StringView value;

	const RewriteUriMode mode;
	const bool stateful;
	const char *const view;

	const struct escape_class *const escape;

	DelayedIstreamControl &delayed;

public:
	UriRewriter(struct pool &_pool,
		    WidgetContext &_ctx,
		    Widget &_widget,
		    StringView _value,
		    RewriteUriMode _mode, bool _stateful,
		    const char *_view,
		    const struct escape_class *_escape,
		    DelayedIstreamControl &_delayed) noexcept
		:pool(_pool), ctx(_ctx), widget(_widget),
		 value(DupBuffer(pool, _value)),
		 mode(_mode), stateful(_stateful),
		 view(_view != nullptr
		      ? (*_view != 0 ? p_strdup(&pool, _view) : "")
		      : nullptr),
		 escape(_escape),
		 delayed(_delayed) {}

	UnusedIstreamPtr Start(WidgetRegistry &widget_registry,
			       UnusedIstreamPtr input) noexcept {
		ResolveWidget(pool,
			      widget,
			      widget_registry,
			      BIND_THIS_METHOD(ResolverCallback),
			      delayed.cancel_ptr);

		return NewTimeoutIstream(pool, std::move(input),
					 ctx.event_loop,
					 inline_widget_body_timeout);
	}

private:
	void ResolverCallback() noexcept;
};

void
UriRewriter::ResolverCallback() noexcept
{
	bool escape_flag = false;
	if (widget.cls != nullptr && widget.HasDefaultView()) {
		const char *uri;

		if (widget.session_sync_pending) {
			RealmSessionLease session(ctx.session_id, ctx.realm);
			if (session)
				widget.LoadFromSession(*session);
			else
				widget.session_sync_pending = false;
		}

		const TempPoolLease tpool;
		bool is_unescaped = value.Find('&') != nullptr;
		if (is_unescaped) {
			char *unescaped = (char *)p_memdup(tpool, value.data, value.size);
			value.size = unescape_inplace(escape, unescaped, value.size);
			value.data = unescaped;
		}

		uri = do_rewrite_widget_uri(pool, ctx, widget,
					    value, mode, stateful,
					    view);
		if (uri != nullptr) {
			value = uri;
			escape_flag = true;
		}
	}

	UnusedIstreamPtr istream;
	if (!value.empty()) {
		istream = istream_memory_new(pool, value.data, value.size);

		if (escape_flag && escape != nullptr)
			istream = istream_escape_new(pool, std::move(istream), *escape);
	} else
		istream = istream_null_new(pool);

	delayed.Set(std::move(istream));
}

/*
 * Constructor: optionally load class, and then call
 * do_rewrite_widget_uri().
 *
 */

UnusedIstreamPtr
rewrite_widget_uri(struct pool &pool,
		   WidgetContext &ctx,
		   Widget &widget,
		   StringView value,
		   RewriteUriMode mode, bool stateful,
		   const char *view,
		   const struct escape_class *escape) noexcept
{
	if (uri_has_authority(value))
		/* can't rewrite if the specified URI is absolute */
		return nullptr;

	if (mode == RewriteUriMode::RESPONSE) {
		auto istream = embed_inline_widget(pool, ctx,
						   nullptr, // TODO
						   true, widget);
		if (escape != nullptr)
			istream = istream_escape_new(pool, std::move(istream), *escape);
		return istream;
	}

	const char *uri;

	if (widget.cls != nullptr) {
		if (!widget.HasDefaultView())
			/* refuse to rewrite URIs when an invalid view name was
			   specified */
			return nullptr;

		const TempPoolLease tpool;
		if (escape != nullptr && !value.IsNull() &&
		    unescape_find(escape, value) != nullptr) {
			char *unescaped = (char *)p_memdup(tpool, value.data, value.size);
			value.size = unescape_inplace(escape, unescaped, value.size);
			value.data = unescaped;
		}

		uri = do_rewrite_widget_uri(pool, ctx, widget, value, mode, stateful,
					    view);
		if (uri == nullptr)
			return nullptr;

		auto istream = istream_string_new(pool, uri);
		if (escape != nullptr)
			istream = istream_escape_new(pool, std::move(istream), *escape);

		return istream;
	} else {
		auto delayed = istream_delayed_new(pool, ctx.event_loop);

		auto rwu = NewFromPool<UriRewriter>(pool, pool, ctx, widget,
						    value, mode, stateful,
						    view, escape, delayed.second);

		return rwu->Start(*ctx.widget_registry, std::move(delayed.first));
	}
}
