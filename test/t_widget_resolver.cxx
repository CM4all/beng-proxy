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

#include "widget/Resolver.hxx"
#include "widget/Registry.hxx"
#include "widget/Widget.hxx"
#include "widget/Ptr.hxx"
#include "widget/Class.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/RootPool.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <assert.h>

static struct Context *global;

struct Context {
	RootPool root_pool;

	struct {
		CancellablePointer cancel_ptr;

		bool finished = false;

		/** abort in the callback? */
		bool abort = false;
	} first, second;

	struct Registry final : Cancellable {
		bool requested = false, finished = false, aborted = false;
		WidgetRegistryCallback callback = nullptr;

		/* virtual methods from class Cancellable */
		void Cancel() noexcept override {
			aborted = true;
		}
	} registry;

	Context() {
		global = this;
	}

	void ResolverCallback1() noexcept;
	void ResolverCallback2() noexcept;
};

void
Widget::DiscardForFocused() noexcept
{
}

const WidgetView *
widget_view_lookup(const WidgetView *view,
		   gcc_unused const char *name) noexcept
{
	return view;
}

void
Context::ResolverCallback1() noexcept
{
	assert(!first.finished);
	assert(!second.finished);

	first.finished = true;

	if (first.abort)
		second.cancel_ptr.Cancel();
}

void
Context::ResolverCallback2() noexcept
{
	assert(first.finished);
	assert(!second.finished);
	assert(!second.abort);

	second.finished = true;
}

/*
 * widget-registry.c emulation
 *
 */

WidgetClassCache::WidgetClassCache(struct pool &parent_pool) noexcept
	:PoolHolder(parent_pool) {}

void
WidgetRegistry::LookupWidgetClass(struct pool &, struct pool &,
				  const char *,
				  WidgetRegistryCallback callback,
				  CancellablePointer &cancel_ptr) noexcept
{
	(void)translation_service; // suppress -Wunused-private-field

	Context *data = global;
	assert(!data->registry.requested);
	assert(!data->registry.finished);
	assert(!data->registry.aborted);
	assert(!data->registry.callback);

	data->registry.requested = true;
	data->registry.callback = callback;
	cancel_ptr = data->registry;
}

static void
widget_registry_finish(Context *data)
{
	assert(data->registry.requested);
	assert(!data->registry.finished);
	assert(!data->registry.aborted);
	assert(data->registry.callback);

	data->registry.finished = true;

	static const WidgetClass cls{};
	data->registry.callback(&cls);
}


/*
 * tests
 *
 */

TEST(WidgetResolver, Normal)
{
	Context data;
	WidgetRegistry registry(data.root_pool,
				*(TranslationService *)(size_t)0x1);

	auto pool = pool_new_linear(data.root_pool, "test", 8192);
	const AllocatorPtr alloc(pool);

	auto widget = MakeWidget(pool, nullptr);
	widget->class_name = "foo";

	ResolveWidget(alloc, *widget, registry,
		      BIND_METHOD(data, &Context::ResolverCallback1),
		      data.first.cancel_ptr);

	ASSERT_FALSE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_FALSE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	widget_registry_finish(&data);

	ASSERT_TRUE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_TRUE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	widget.reset();
	pool.reset();
	pool_commit();
}

TEST(WidgetResolver, Abort)
{
	Context data;
	WidgetRegistry registry(data.root_pool,
				*(TranslationService *)(size_t)0x1);

	auto pool = pool_new_linear(data.root_pool, "test", 8192);
	const AllocatorPtr alloc(pool);

	auto widget = MakeWidget(pool, nullptr);
	widget->class_name = "foo";

	ResolveWidget(alloc, *widget, registry,
		      BIND_METHOD(data, &Context::ResolverCallback1),
		      data.first.cancel_ptr);

	ASSERT_FALSE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_FALSE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	data.first.cancel_ptr.Cancel();

	ASSERT_FALSE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_FALSE(data.registry.finished);
	ASSERT_TRUE(data.registry.aborted);

	widget.reset();
	pool.reset();
	pool_commit();
}

TEST(WidgetResolver, TwoClients)
{
	Context data;
	WidgetRegistry registry(data.root_pool,
				*(TranslationService *)(size_t)0x1);

	auto pool = pool_new_linear(data.root_pool, "test", 8192);
	const AllocatorPtr alloc(pool);

	auto widget = MakeWidget(pool, nullptr);
	widget->class_name = "foo";

	ResolveWidget(alloc, *widget, registry,
		      BIND_METHOD(data, &Context::ResolverCallback1),
		      data.first.cancel_ptr);

	ResolveWidget(alloc, *widget, registry,
		      BIND_METHOD(data, &Context::ResolverCallback2),
		      data.second.cancel_ptr);

	ASSERT_FALSE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_FALSE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	widget_registry_finish(&data);

	ASSERT_TRUE(data.first.finished);
	ASSERT_TRUE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_TRUE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	widget.reset();
	pool.reset();
	pool_commit();
}

TEST(WidgetResolver, TwoAbort)
{
	Context data;
	data.first.abort = true;

	WidgetRegistry registry(data.root_pool,
				*(TranslationService *)(size_t)0x1);

	auto pool = pool_new_linear(data.root_pool, "test", 8192);
	const AllocatorPtr alloc(pool);

	auto widget = MakeWidget(pool, nullptr);
	widget->class_name = "foo";

	ResolveWidget(alloc, *widget, registry,
		      BIND_METHOD(data, &Context::ResolverCallback1),
		      data.first.cancel_ptr);

	ResolveWidget(alloc, *widget, registry,
		      BIND_METHOD(data, &Context::ResolverCallback2),
		      data.second.cancel_ptr);

	ASSERT_FALSE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_FALSE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	widget_registry_finish(&data);

	ASSERT_TRUE(data.first.finished);
	ASSERT_FALSE(data.second.finished);
	ASSERT_TRUE(data.registry.requested);
	ASSERT_TRUE(data.registry.finished);
	ASSERT_FALSE(data.registry.aborted);

	widget.reset();
	pool.reset();
	pool_commit();
}
