// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Cache.hxx"
#include "util/BindMethod.hxx"

struct pool;
class TranslationService;
class CancellablePointer;
struct WidgetClass;

typedef BoundMethod<void(const WidgetClass *cls) noexcept> WidgetRegistryCallback;

/**
 * Interface for the widget registry managed by the translation
 * server.
 */
class WidgetRegistry {
	TranslationService &translation_service;

	WidgetClassCache cache;

public:
	explicit WidgetRegistry(struct pool &parent_pool,
				TranslationService &_translation_service) noexcept
		:translation_service(_translation_service),
		 cache(parent_pool) {}

	void FlushCache() noexcept {
		cache.Clear();
	}

	void LookupWidgetClass(struct pool &caller_pool, struct pool &widget_pool,
			       const char *name,
			       WidgetRegistryCallback callback,
			       CancellablePointer &cancel_ptr) noexcept;
};
