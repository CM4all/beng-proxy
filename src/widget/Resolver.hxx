// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/BindMethod.hxx"

class AllocatorPtr;
class Widget;
class WidgetRegistry;
class CancellablePointer;

typedef BoundMethod<void() noexcept> WidgetResolverCallback;

/**
 * Wrapper for widget-registry.h which resolves widget classes.  This
 * library can manage several concurrent requests for one widget
 * object.
 */
void
ResolveWidget(AllocatorPtr alloc,
	      Widget &widget,
	      WidgetRegistry &registry,
	      WidgetResolverCallback callback,
	      CancellablePointer &cancel_ptr) noexcept;
