/*
 * Copyright 2007-2017 Content Management AG
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

#include "istream/UnusedHoldPtr.hxx"
#include "pool/LeakDetector.hxx"
#include "io/Logger.hxx"
#include "http/Method.h"
#include "util/IntrusiveForwardList.hxx"
#include "util/StringView.hxx"
#include "util/Compiler.h"

#include <cstdint>
#include <memory>

struct pool;
class AllocatorPtr;
class StringMap;
struct RealmSession;
struct WidgetSession;
struct WidgetView;
struct WidgetClass;
struct WidgetRef;
struct ResourceAddress;
class WidgetResolver;
class LimitedConcurrencyQueue;

/**
 * A widget instance.
 */
class Widget final
	: PoolLeakDetector,
	  public IntrusiveForwardListHook
{
public:
	IntrusiveForwardList<Widget> children;

	Widget *parent = nullptr;

	/**
	 * This class throttles concurrent requests to child widgets.
	 */
	std::unique_ptr<LimitedConcurrencyQueue> child_throttler;

	struct pool &pool;

	const char *class_name = nullptr;

	const char *quoted_class_name = nullptr;

	/**
	 * The widget class.  May be nullptr if the #class_name hasn't been
	 * looked up yet.
	 */
	const WidgetClass *cls;

	/**
	 * The object that is currently requesting the widget class from
	 * the translation server.
	 */
	WidgetResolver *resolver = nullptr;

	/** the widget's instance id, as specified in the template */
	const char *id = nullptr;

	/**
	 * A chain of widget ids which identify this widget in the
	 * top-level template.
	 */
	const char *id_path = nullptr;

	/**
	 * A prefix for this widget's XML ids, unique in the top-level
	 * template.
	 */
	const char *prefix = nullptr;

	/** in which form should this widget be displayed? */
	enum class Display : uint8_t {
		INLINE,
		NONE,
	} display = Display::INLINE;

	/**
	 * The approval level for embedding this widget into its
	 * container.  This is based on #TRANSLATE_SELF_CONTAINER and
	 * #TRANSLATE_GROUP_CONTAINER.
	 */
	enum class Approval : uint8_t {
		/**
		 * Approval was given.
		 */
		GIVEN,

		/**
		 * Approval was denied.
		 */
		DENIED,

		/**
		 * Approval has not been verified yet.
		 */
		UNKNOWN,
	} approval = Approval::GIVEN;

	/** what is the scope of session data? */
	enum class SessionScope : uint8_t {
		/** each resource has its own set of widget sessions */
		RESOURCE,

		/** all resources on this site share the same widget sessions */
		SITE,
	} session_scope = SessionScope::RESOURCE;

	/**
	 * This is set to true by the widget resolver when the widget
	 * class is "stateful".  It means that widget_sync_session() must
	 * be called, which in turn resets the flag.  It protects against
	 * calling widget_sync_session() twice.
	 */
	bool session_sync_pending = false;

	/**
	 * This is set to true by widget_sync_session(), and is checked by
	 * widget_response_response().  The current request will only be
	 * saved to the session if the actual response from the widget
	 * server is processable.
	 */
	bool session_save_pending = false;

	/**
	 * Widget attributes specified by the template.  Some of them can
	 * be overridden by the HTTP client.
	 */
	struct FromTemplate {
		/** the path info as specified in the template */
		const char *path_info = "";

		/** the query string as specified in the template */
		const char *query_string = nullptr;

		/** HTTP request headers specified in the template */
		StringMap *headers = nullptr;

		/** the name of the view specified in the template */
		const char *view_name = nullptr;

		/**
		 * The view that was specified in the template.  This attribute is
		 * undefined before the widget resolver finishes.  Being nullptr is a
		 * fatal error, and means that no operation is possible on this
		 * widget.
		 */
		const WidgetView *view;
	} from_template;

	/**
	 * Parameters that were forwarded from the HTTP request to this
	 * widget.
	 */
	struct {
		/**
		 * A reference to the focused widget relative to this one.
		 * nullptr when the focused widget is not an (indirect) child of
		 * this one.
		 */
		const WidgetRef *focus_ref = nullptr;

		/**
		 * The new path_info for the focused widget.
		 */
		const char *path_info = nullptr;

		/** the query string provided by the browser */
		StringView query_string = nullptr;

		/** the request body */
		UnusedHoldIstreamPtr body;

		/**
		 * The view requested by the client.  If no view was
		 * explicitly requested, then this is the view selected by the
		 * template.  This attribute is undefined before the widget
		 * resolver finishes.
		 */
		const WidgetView *view;

		/**
		 * The request's HTTP method if the widget is focused.  Falls
		 * back to HTTP_METHOD_GET if the widget is not focused.
		 */
		http_method_t method = HTTP_METHOD_GET;

		/**
		 * Is this the "top frame" widget requested by the client?
		 */
		bool frame = false;

		/**
		 * This flag is set when the view selected by the client is
		 * unauthorized, and will only be allowed when the widget
		 * response is not processable.  If it is, we might expose
		 * internal widget parameters by switching off the processor.
		 */
		bool unauthorized_view = false;
	} from_request;

	/**
	 * Parameters that will forwarded from the HTTP request to the
	 * focused widget (which is an (indirect) child of this widget).
	 */
	struct ForFocused {
		/**
		 * The new path_info for the focused widget.
		 */
		const char *const path_info;

		/**
		 * The query string for the focused widget.
		 */
		const StringView query_string;

		/**
		 * The request body.  This must be closed if it failed to be
		 * submitted to the focused widget.
		 */
		UnusedHoldIstreamPtr body;

		/**
		 * The HTTP method to be invoked on the focused widget.
		 */
		const http_method_t method;

		ForFocused(http_method_t _method, const char *_path_info,
			   StringView _query_string,
			   UnusedHoldIstreamPtr &&_body) noexcept
			:path_info(_path_info), query_string(_query_string),
			 body(std::move(_body)),
			 method(_method) {}
	} *for_focused = nullptr;

private:
	/**
	 * Cached attributes that will be initialized lazily.
	 */
	mutable struct {
		const char *log_name = nullptr;

		/** the address which is actually retrieved - this is the same
		    as class->address, except when the user clicked on a
		    relative link */
		const ResourceAddress *address = nullptr;

		/**
		 * The widget address including path_info and the query string
		 * from the template.  See widget_stateless_address().
		 */
		const ResourceAddress *stateless_address = nullptr;
	} lazy;

	struct LoggerDomain {
		gcc_pure
		StringView GetDomain() const noexcept;
	};

public:
	BasicLogger<LoggerDomain> logger;

	Widget(struct pool &_pool, const WidgetClass *_cls) noexcept;

	struct RootTag {};
	Widget(RootTag, struct pool &_pool, const char *_id) noexcept;

	~Widget() noexcept;

	Widget(const Widget &) = delete;
	Widget &operator=(const Widget &) = delete;

	void SetId(StringView _id) noexcept;
	void SetClassName(StringView _class_name) noexcept;

	const char *GetIdPath() const noexcept {
		return id_path;
	}

	const char *GetPrefix() const noexcept {
		return prefix;
	}

	const char *GetQuotedClassName() const noexcept {
		return quoted_class_name;
	}

	/**
	 * Clear the lazy-initialized attributes.  This is meant for unit
	 * tests only, do not use in production code.
	 */
	void ClearLazy() noexcept {
		lazy = {};
	}

	/**
	 * Returns this widget's name for log/error messages.
	 */
	gcc_pure
	const char *GetLogName() const noexcept;

	gcc_pure
	Widget *FindRoot() noexcept {
		Widget *w = this;
		while (w->parent != nullptr)
			w = w->parent;
		return w;
	}

	gcc_pure
	Widget *FindChild(const char *child_id) noexcept;

	gcc_pure
	const char *GetDefaultPathInfo() const noexcept {
		return from_template.path_info;
	}

	gcc_pure
	const char *GetRequestedPathInfo() const noexcept {
		return from_request.path_info != nullptr
			? from_request.path_info
			: from_template.path_info;
	}

	gcc_pure
	const char *GetPathInfo(bool stateful) const noexcept {
		return stateful ? GetRequestedPathInfo() : GetDefaultPathInfo();
	}

	gcc_pure
	bool HasDefaultView() const noexcept {
		return from_template.view != nullptr;
	}

	/**
	 * Returns the view that will be used according to the widget
	 * class and the view specification in the parent.  It ignores the
	 * view name from the request.
	 */
	const WidgetView *GetDefaultView() const noexcept {
		return from_template.view;
	}

	/**
	 * Is the default view a container?
	 */
	gcc_pure
	bool IsContainerByDefault() const noexcept;

	/**
	 * Returns the view that is used to determine the address of the
	 * server.
	 */
	const WidgetView *GetAddressView() const noexcept {
		return GetDefaultView();
	}

	gcc_pure
	const WidgetView *GetEffectiveView() const noexcept {
		return from_request.view;
	}

	gcc_pure
	bool HasFocus() const noexcept;

	gcc_pure
	bool DescendantHasFocus() const noexcept;

	/**
	 * Does the effective view enable the HTML processor?
	 */
	gcc_pure
	bool HasProcessor() const noexcept;

	/**
	 * Is the effective view a container?
	 */
	gcc_pure
	bool IsContainer() const noexcept;

	/**
	 * Returns the view that is used to determine the transformations of
	 * the response.
	 */
	const WidgetView *GetTransformationView() const noexcept {
		return GetEffectiveView();
	}

	/**
	 * Determines whether it is allowed to embed the widget in a page
	 * with the specified host name.  If not, throws a
	 * std::runtime_error with an explanatory message.
	 */
	void CheckHost(const char *host, const char *site_name) const;

	const ResourceAddress &DetermineAddress(bool stateful) const noexcept;

	gcc_pure
	const ResourceAddress &GetAddress() const noexcept {
		if (lazy.address == nullptr)
			lazy.address = &DetermineAddress(true);

		return *lazy.address;
	}

	gcc_pure
	const ResourceAddress &GetStatelessAddress() const noexcept {
		if (lazy.stateless_address == nullptr)
			lazy.stateless_address = &DetermineAddress(false);

		return *lazy.stateless_address;
	}

	gcc_pure
	ResourceAddress GetBaseAddress(AllocatorPtr alloc,
				       bool stateful) const noexcept;

	gcc_pure
	const char *AbsoluteUri(AllocatorPtr alloc, bool stateful,
				StringView relative_uri) const noexcept;

	/**
	 * Returns an URI relative to the widget base address.
	 */
	gcc_pure
	StringView RelativeUri(AllocatorPtr alloc, bool stateful,
			       StringView relative_uri) const noexcept;

	gcc_pure
	const char *ExternalUri(AllocatorPtr alloc,
				StringView external_base_uri,
				const StringMap *args,
				bool stateful,
				StringView relative_uri,
				const char *frame,
				const char *view) const noexcept;

	/**
	 * Discard data for the focused widget (which is a descendant of
	 * this widget and was not yet found).
	 */
	void DiscardForFocused() noexcept;

	bool InitApproval(bool self_container) noexcept;

	/**
	 * Check the "approval" value.  If it is
	 * #WIDGET_APPROVAL_UNKNOWN, check the widget group approval
	 * of the parent widget.  This is a postponed check because a
	 * widget's group is only known after its widget class has
	 * been looked up.
	 *
	 * @return true if the widget is approved
	 */
	gcc_pure
	bool CheckApproval() noexcept;

	/**
	 * Free important resources associated with the widget.  A widget
	 * callback must call this function on a widget which it will not
	 * send a HTTP request to.
	 */
	void Cancel() noexcept;

	/**
	 * Copy parameters from the request to the widget.
	 *
	 * Throws #WidgetError on error.
	 */
	void CopyFromRequest();

	/**
	 * Returns the widget's session object.  The passed session object
	 * must be locked.
	 */
	WidgetSession *GetSession(RealmSession &session, bool create) noexcept;

	gcc_pure
	bool ShouldSyncSession() const noexcept {
		if (from_request.body)
			/* do not save to session when this is a POST request */
			return false;

		/* save to session only if the effective view features the HTML
		   processor */
		if (!HasProcessor())
			return false;

		return true;
	}

	/** copy data from the widget to its associated session */
	void SaveToSession(WidgetSession &ws) const noexcept;

	/**
	 * Save the current request to the session.  Call this after you
	 * have received the widget's response if the session_save_pending
	 * flag was set by LoadFromSession().
	 */
	void SaveToSession(RealmSession &session) noexcept;

	/** restore data from the session */
	void LoadFromSession(const WidgetSession &ws) noexcept;
	void LoadFromSession(RealmSession &session) noexcept;

	/**
	 * Overwrite request data, copy values from a HTTP redirect
	 * location.
	 */
	void CopyFromRedirectLocation(StringView location,
				      RealmSession *session) noexcept;

	struct Disposer {
		void operator()(Widget *widget) noexcept {
			widget->~Widget();
		}
	};
};

/**
 * Recursion detection: check if the widget or its parent chain
 * contains the specified class name.
 */
gcc_pure
bool
widget_check_recursion(const Widget *widget) noexcept;
