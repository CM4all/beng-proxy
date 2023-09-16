// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Resolver.hxx"
#include "Registry.hxx"
#include "Widget.hxx"
#include "Class.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "util/DestructObserver.hxx"
#include "util/IntrusiveList.hxx"
#include "AllocatorPtr.hxx"

class WidgetResolver;

class WidgetResolverListener final
	: public IntrusiveListHook<IntrusiveHookMode::NORMAL>,
	  Cancellable {

	WidgetResolver &resolver;

	const WidgetResolverCallback callback;

#ifndef NDEBUG
	bool finished = false, aborted = false;
#endif

public:
	WidgetResolverListener(WidgetResolver &_resolver,
			       WidgetResolverCallback _callback,
			       CancellablePointer &cancel_ptr) noexcept
		:resolver(_resolver),
		 callback(_callback) {
		cancel_ptr = *this;
	}

	void Finish() noexcept;

private:
	void Destroy() noexcept {
		this->~WidgetResolverListener();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;
};

class WidgetResolver final : DestructAnchor {
	Widget &widget;

	IntrusiveList<WidgetResolverListener> listeners;

	CancellablePointer cancel_ptr;

	bool finished = false;

#ifndef NDEBUG
	bool aborted = false;
#endif

public:
	explicit WidgetResolver(Widget &_widget) noexcept
		:widget(_widget) {}

	bool IsFinished() const noexcept {
		return finished;
	}

	void Start(WidgetRegistry &registry) noexcept {
		/* use the widget pool because the listener pool may be
		   aborted, while the others still run */
		registry.LookupWidgetClass(widget.pool, widget.pool,
					   widget.class_name,
					   BIND_THIS_METHOD(RegistryCallback),
					   cancel_ptr);
	}

	void AddListener(WidgetResolverListener &listener) noexcept {
		assert(!finished);

		listeners.push_back(listener);
	}

	void RemoveListener(WidgetResolverListener &listener) noexcept;

private:
	void Destroy() noexcept {
		this->~WidgetResolver();
	}

	void Abort() noexcept;

	void RegistryCallback(const WidgetClass *cls) noexcept;
};

void
WidgetResolver::RemoveListener(WidgetResolverListener &listener) noexcept
{
	assert(widget.resolver == this);
	assert(!listeners.empty());
	assert(!aborted);

	listener.unlink();

	if (listeners.empty()) {
		/* the last listener has been aborted: abort the widget
		   registry */
		if (finished)
			/* destroy the resolver before returning from
			   WidgetResolverListener::Cancel() because its caller may
			   destroy the memory pool */
			Destroy();
		else
			Abort();
	}
}

void
WidgetResolver::Abort() noexcept
{
	assert(listeners.empty());
	assert(widget.resolver == this);

#ifndef NDEBUG
	aborted = true;
#endif

	widget.resolver = nullptr;
	cancel_ptr.Cancel();
	Destroy();
}

/*
 * async operation
 *
 */

void
WidgetResolverListener::Cancel() noexcept
{
	assert(!finished);
	assert(!aborted);

#ifndef NDEBUG
	aborted = true;
#endif

	resolver.RemoveListener(*this);

	Destroy();
}


/*
 * registry callback
 *
 */

inline void
WidgetResolverListener::Finish() noexcept
{
	assert(!finished);
	assert(!aborted);

#ifndef NDEBUG
	finished = true;
#endif

	/* copy the callback to the stack, destroy this object and
	   then invoke the callback; this ordering is important
	   because the callback may free all memory */
	const auto _callback = callback;
	Destroy();
	_callback();
}

void
WidgetResolver::RegistryCallback(const WidgetClass *cls) noexcept
{
	assert(widget.cls == nullptr);
	assert(widget.resolver == this);
	assert(!listeners.empty());
	assert(!finished);
	assert(!aborted);

	finished = true;

	widget.cls = cls;

	widget.from_template.view = widget.from_request.view = cls != nullptr
		? widget_view_lookup(cls->views, widget.from_template.view_name)
		: nullptr;

	widget.session_sync_pending = cls != nullptr && cls->stateful &&
		/* the widget session code requires a valid view */
		widget.from_template.view != nullptr;

	const DestructObserver destructed(*this);

	do {
		assert(!listeners.empty());
		auto &l = listeners.front();
		listeners.pop_front();

		if (listeners.empty())
			/* destruct this object before invoking the last callback
			   because the callback may free the memory pool */
			Destroy();

		l.Finish();

		if (destructed)
			return;
	} while (!listeners.empty());

	/* this is reachable only if the last listener has been canceled
	   from within the previous listener callback */
	Destroy();
}


/*
 * constructor
 *
 */

static WidgetResolver *
widget_resolver_alloc(Widget &widget) noexcept
{
	return widget.resolver = NewFromPool<WidgetResolver>(widget.pool, widget);
}

void
ResolveWidget(AllocatorPtr alloc,
	      Widget &widget,
	      WidgetRegistry &registry,
	      WidgetResolverCallback callback,
	      CancellablePointer &cancel_ptr) noexcept
{
	bool is_new = false;

	assert(widget.class_name != nullptr);
	assert(pool_contains(widget.pool, &widget, sizeof(widget)));

	if (widget.cls != nullptr) {
		/* already resolved successfully */
		callback();
		return;
	}

	/* create new resolver object if it does not already exist */

	WidgetResolver *resolver = widget.resolver;
	if (resolver == nullptr) {
		resolver = widget_resolver_alloc(widget);
		is_new = true;
	} else if (resolver->IsFinished()) {
		/* we have already failed to resolve this widget class; return
		   immediately, don't try again */
		callback();
		return;
	}

	assert(pool_contains(widget.pool, widget.resolver,
			     sizeof(*widget.resolver)));

	/* add a new listener to the resolver */

	auto listener = alloc.New<WidgetResolverListener>(*resolver,
							  callback,
							  cancel_ptr);

	resolver->AddListener(*listener);

	/* finally send request to the widget registry */

	if (is_new)
		resolver->Start(registry);
}
