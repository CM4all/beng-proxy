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
