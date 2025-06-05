// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;

/** a reference to a widget inside a widget.  nullptr means the current
    (root) widget is being referenced */
struct WidgetRef {
	const WidgetRef *next;

	const char *id;
};

constexpr char WIDGET_REF_SEPARATOR = ':';

[[gnu::pure]]
const WidgetRef *
widget_ref_parse(AllocatorPtr alloc, const char *p) noexcept;

/**
 * Is the specified "inner" reference inside or the same as "outer"?
 */
[[gnu::pure]]
bool
widget_ref_includes(const WidgetRef *outer,
		    const WidgetRef *inner) noexcept;
