// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Registry.hxx"
#include "Class.hxx"
#include "View.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "AllocatorPtr.hxx"
#include "io/Logger.hxx"
#include "stopwatch.hxx"

static void
widget_registry_lookup(struct pool &caller_pool, struct pool &widget_pool,
		       TranslationService &service,
		       const char *widget_type,
		       TranslateHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
{
	auto request = NewFromPool<TranslateRequest>(caller_pool);

	request->widget_type = widget_type;

	service.SendRequest(widget_pool, *request,
			    nullptr, // TODO
			    handler, cancel_ptr);
}

class WidgetRegistryLookup final : TranslateHandler {
	struct pool &widget_pool;

	WidgetClassCache &cache;

	const char *const name;

	const WidgetRegistryCallback callback;

public:
	WidgetRegistryLookup(struct pool &_widget_pool, WidgetClassCache &_cache,
			     const char *_name,
			     WidgetRegistryCallback _callback) noexcept
		:widget_pool(_widget_pool), cache(_cache),
		 name(_name), callback(_callback) {}

	void Start(struct pool &caller_pool, TranslationService &service,
		   CancellablePointer &cancel_ptr) noexcept {
		widget_registry_lookup(caller_pool, widget_pool,
				       service, name,
				       *this, cancel_ptr);
	}

private:
	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
WidgetRegistryLookup::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	auto &response = *_response;

	assert(response.views != nullptr);

	if (response.status != HttpStatus{}) {
		callback(nullptr);
		return;
	}

	auto cls = NewFromPool<WidgetClass>(widget_pool);
	cls->local_uri = response.local_uri;
	cls->untrusted_host = response.untrusted;
	cls->untrusted_prefix = response.untrusted_prefix;
	cls->untrusted_site_suffix = response.untrusted_site_suffix;
	cls->untrusted_raw_site_suffix = response.untrusted_raw_site_suffix;
	if (cls->untrusted_host == nullptr)
		/* compatibility with v0.7.16 */
		cls->untrusted_host = response.host;
	cls->cookie_host = response.cookie_host;
	cls->group = response.widget_group;
	cls->container_groups = std::move(response.container_groups);
	cls->direct_addressing = response.direct_addressing;
	cls->stateful = response.stateful;
	cls->require_csrf_token = response.require_csrf_token;
	cls->anchor_absolute = response.anchor_absolute;
	cls->info_headers = response.widget_info;
	cls->dump_headers = response.dump_headers;
	cls->views = response.views->CloneChain(widget_pool);

	cache.Put(name, *cls);

	callback(cls);
}

void
WidgetRegistryLookup::OnTranslateError(std::exception_ptr ep) noexcept
{
	LogConcat(2, "WidgetRegistry", ep);

	callback(nullptr);
}

void
WidgetRegistry::LookupWidgetClass(struct pool &caller_pool,
				  struct pool &widget_pool,
				  const char *widget_type,
				  WidgetRegistryCallback callback,
				  CancellablePointer &cancel_ptr) noexcept
{
	assert(widget_type != nullptr);

	const auto *cls = cache.Get(widget_type);
	if (cls != nullptr) {
		callback(NewFromPool<WidgetClass>(widget_pool, widget_pool, *cls));
		return;
	}

	auto lookup = NewFromPool<WidgetRegistryLookup>(caller_pool, widget_pool,
							cache, widget_type,
							callback);
	lookup->Start(caller_pool, translation_service, cancel_ptr);
}
