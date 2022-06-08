/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Registry.hxx"
#include "Class.hxx"
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
	void OnTranslateResponse(TranslateResponse &response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
WidgetRegistryLookup::OnTranslateResponse(TranslateResponse &response) noexcept
{
	assert(response.views != nullptr);

	if (response.status != 0) {
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
	cls->views.CopyChainFrom(widget_pool, *response.views);

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
