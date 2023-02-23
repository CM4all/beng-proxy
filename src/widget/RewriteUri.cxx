// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RewriteUri.hxx"
#include "Widget.hxx"
#include "Resolver.hxx"
#include "Class.hxx"
#include "Context.hxx"
#include "Inline.hxx"
#include "uri/Extract.hxx"
#include "pool/LeakDetector.hxx"
#include "pool/SharedPtr.hxx"
#include "pool/tpool.hxx"
#include "escape/Istream.hxx"
#include "escape/Class.hxx"
#include "escape/Pool.hxx"
#include "istream/TimeoutIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "strmap.hxx"
#include "bp/session/Lease.hxx"
#include "util/SpanCast.hxx"
#include "util/Cancellable.hxx"
#include "util/StringCompare.hxx"
#include "util/StringSplit.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

using std::string_view_literals::operator""sv;

RewriteUriMode
parse_uri_mode(const std::string_view s) noexcept
{
	if (s == "direct"sv)
		return RewriteUriMode::DIRECT;
	else if (s == "focus"sv)
		return RewriteUriMode::FOCUS;
	else if (s == "partial"sv)
		return RewriteUriMode::PARTIAL;
	else if (s == "response"sv)
		return RewriteUriMode::RESPONSE;
	else
		return RewriteUriMode::PARTIAL;
}

/*
 * The "real" rewriting code
 *
 */

static constexpr std::string_view
MakeStringView(const char *begin, const char *end) noexcept
{
	return {begin, std::size_t(end - begin)};
}

static const char *
uri_replace_hostname(AllocatorPtr alloc, const std::string_view uri,
		     const char *hostname) noexcept
{
	assert(hostname != nullptr);

	const std::string_view old_host = UriHostAndPort(uri);
	if (old_host.data() == nullptr)
		return uri.starts_with('/')
			? alloc.Concat("//", hostname, uri)
			: nullptr;

	const auto colon = old_host.find(':');
	const auto end_pos = colon != old_host.npos ? colon : old_host.size();
	const char *end = old_host.data() + end_pos;

	return alloc.Concat(Partition(uri, old_host.data()).first,
			    hostname,
			    Partition(uri, end).second);
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

		const auto host = UriHostAndPort(absolute_uri);
		if (host.data() == nullptr)
			return uri;

		return alloc.Concat(MakeStringView(absolute_uri, host.data()),
				    untrusted_prefix,
				    '.', host, uri);
	}

	const auto host = UriHostAndPort(uri);
	if (host.data() == nullptr)
		return uri;

	return alloc.Concat(MakeStringView(uri, host.data()),
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

	const char *path = UriPathQueryFragment(uri);
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

	const char *path = UriPathQueryFragment(uri);
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
		      std::string_view value,
		      RewriteUriMode mode, bool stateful,
		      const char *view) noexcept
{
	if (widget.cls->local_uri != nullptr && SkipPrefix(value, "@/"sv))
		/* relative to widget's "local URI" */
		return alloc.Concat(widget.cls->local_uri, value);

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

class UriRewriter final : PoolLeakDetector, Cancellable {
	const AllocatorPtr alloc;
	const SharedPoolPtr<WidgetContext> ctx;
	Widget &widget;

	/** the value passed to rewrite_widget_uri() */
	std::string_view value;

	const RewriteUriMode mode;
	const bool stateful;
	const char *const view;

	const struct escape_class *const escape;

	DelayedIstreamControl &delayed;

	CancellablePointer cancel_ptr;

public:
	UriRewriter(AllocatorPtr _alloc,
		    SharedPoolPtr<WidgetContext> &&_ctx,
		    Widget &_widget,
		    std::string_view _value,
		    RewriteUriMode _mode, bool _stateful,
		    const char *_view,
		    const struct escape_class *_escape,
		    DelayedIstreamControl &_delayed) noexcept
		:PoolLeakDetector(_alloc),
		 alloc(_alloc), ctx(std::move(_ctx)), widget(_widget),
		 value(alloc.Dup(_value)),
		 mode(_mode), stateful(_stateful),
		 view(_view != nullptr
		      ? (*_view != 0 ? alloc.Dup(_view) : "")
		      : nullptr),
		 escape(_escape),
		 delayed(_delayed)
	{
		delayed.cancel_ptr = *this;
	}

	void Destroy() noexcept {
		this->~UriRewriter();
	}

	UnusedIstreamPtr Start(UnusedIstreamPtr input) noexcept {
		auto &pool = alloc.GetPool();
		auto &event_loop = ctx->event_loop;

		ResolveWidget(alloc,
			      widget,
			      *ctx->widget_registry,
			      BIND_THIS_METHOD(ResolverCallback),
			      cancel_ptr);

		return NewTimeoutIstream(pool, std::move(input),
					 event_loop,
					 inline_widget_body_timeout);
	}

private:
	void ResolverCallback() noexcept;

	void Cancel() noexcept override {
		auto _cancel_ptr = std::move(cancel_ptr);
		Destroy();
		_cancel_ptr.Cancel();
	}
};

void
UriRewriter::ResolverCallback() noexcept
{
	bool escape_flag = false;
	if (widget.cls != nullptr && widget.HasDefaultView()) {
		const char *uri;

		if (widget.session_sync_pending) {
			auto session = ctx->GetRealmSession();
			if (session)
				widget.LoadFromSession(*session);
			else
				widget.session_sync_pending = false;
		}

		const TempPoolLease tpool;
		bool is_unescaped = value.find('&') != value.npos;
		if (is_unescaped)
			value = unescape_dup(*tpool, *escape, value);

		uri = do_rewrite_widget_uri(alloc, *ctx, widget,
					    value, mode, stateful,
					    view);
		if (uri != nullptr) {
			value = uri;
			escape_flag = true;
		}
	}

	auto &pool = alloc.GetPool();

	UnusedIstreamPtr istream;
	if (!value.empty()) {
		istream = istream_memory_new(pool, AsBytes(value));

		if (escape_flag && escape != nullptr)
			istream = istream_escape_new(pool, std::move(istream), *escape);
	} else
		istream = istream_null_new(pool);

	auto &_delayed = delayed;
	Destroy();
	_delayed.Set(std::move(istream));
}

/*
 * Constructor: optionally load class, and then call
 * do_rewrite_widget_uri().
 *
 */

UnusedIstreamPtr
rewrite_widget_uri(struct pool &pool,
		   SharedPoolPtr<WidgetContext> ctx,
		   const StopwatchPtr &parent_stopwatch,
		   Widget &widget,
		   std::string_view  value,
		   RewriteUriMode mode, bool stateful,
		   const char *view,
		   const struct escape_class *escape) noexcept
{
	if (UriHasAuthority(value))
		/* can't rewrite if the specified URI is absolute */
		return nullptr;

	if (mode == RewriteUriMode::RESPONSE) {
		auto istream = embed_inline_widget(pool, std::move(ctx),
						   parent_stopwatch,
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
		if (escape != nullptr && value.data() != nullptr &&
		    unescape_find(escape, value) != nullptr)
			value = unescape_dup(*tpool, *escape, value);

		uri = do_rewrite_widget_uri(pool, *ctx, widget, value,
					    mode, stateful,
					    view);
		if (uri == nullptr)
			return nullptr;

		auto istream = istream_string_new(pool, uri);
		if (escape != nullptr)
			istream = istream_escape_new(pool, std::move(istream), *escape);

		return istream;
	} else {
		auto delayed = istream_delayed_new(pool, ctx->event_loop);

		auto rwu = NewFromPool<UriRewriter>(pool, pool, std::move(ctx), widget,
						    value, mode, stateful,
						    view, escape, delayed.second);

		return rwu->Start(std::move(delayed.first));
	}
}
