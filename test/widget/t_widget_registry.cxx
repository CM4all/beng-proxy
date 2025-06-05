// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "widget/Registry.hxx"
#include "widget/Widget.hxx"
#include "widget/View.hxx"
#include "widget/Class.hxx"
#include "http/Address.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "PInstance.hxx"
#include "util/Cancellable.hxx"
#include "util/StringAPI.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

class MyTranslationService final : public TranslationService, Cancellable {
public:
	bool aborted = false;

	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		aborted = true;
	}
};

struct Context : PInstance {
	bool got_class = false;
	const WidgetClass *cls = nullptr;

	void RegistryCallback(const WidgetClass *_cls) noexcept {
		got_class = true;
		cls = _cls;
	}
};

/*
 * tstock.c emulation
 *
 */

void
MyTranslationService::SendRequest(AllocatorPtr alloc,
				  const TranslateRequest &request,
				  const StopwatchPtr &,
				  TranslateHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
{
	assert(request.remote_host == NULL);
	assert(request.host == NULL);
	assert(request.uri == NULL);
	assert(request.widget_type != NULL);
	assert(request.session.data() == nullptr);
	assert(request.param == NULL);

	if (StringIsEqual(request.widget_type, "sync")) {
		auto response = UniquePoolPtr<TranslateResponse>::Make(alloc.GetPool());
		response->address = *http_address_parse(alloc, "http://foo/");
		response->views.push_front(*alloc.New<WidgetView>(nullptr));
		response->views.front().address = {ShallowCopy(), response->address};
		handler.OnTranslateResponse(std::move(response));
	} else if (StringIsEqual(request.widget_type, "block")) {
		cancel_ptr = *this;
	} else
		assert(0);
}


/*
 * tests
 *
 */

TEST(WidgetRegistry, Normal)
{
	MyTranslationService ts;
	Context data;
	WidgetRegistry registry(data.root_pool, ts);
	CancellablePointer cancel_ptr;

	auto pool = pool_new_linear(data.root_pool, "test", 8192);

	registry.LookupWidgetClass(pool, pool, "sync",
				   BIND_METHOD(data, &Context::RegistryCallback),
				   cancel_ptr);
	ASSERT_FALSE(ts.aborted);
	ASSERT_TRUE(data.got_class);
	ASSERT_NE(data.cls, nullptr);
	ASSERT_FALSE(data.cls->views.empty());

	auto &view = data.cls->views.front();
	ASSERT_EQ(view.address.type, ResourceAddress::Type::HTTP);
	ASSERT_STREQ(view.address.GetHttp().host_and_port, "foo");
	ASSERT_STREQ(view.address.GetHttp().path, "/");
	ASSERT_EQ(std::next(data.cls->views.begin()), data.cls->views.end());
	ASSERT_TRUE(view.transformations.empty());

	pool.reset();
	pool_commit();
}

/** caller aborts */
TEST(WidgetRegistry, Abort)
{
	MyTranslationService ts;
	Context data;
	WidgetRegistry registry(data.root_pool, ts);
	CancellablePointer cancel_ptr;

	auto pool = pool_new_linear(data.root_pool, "test", 8192);

	registry.LookupWidgetClass(pool, pool,  "block",
				   BIND_METHOD(data, &Context::RegistryCallback),
				   cancel_ptr);
	ASSERT_FALSE(data.got_class);
	ASSERT_FALSE(ts.aborted);

	cancel_ptr.Cancel();

	ASSERT_TRUE(ts.aborted);
	ASSERT_FALSE(data.got_class);

	pool.reset();
	pool_commit();
}
