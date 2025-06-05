// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
struct WidgetContext;
class UnusedIstreamPtr;
class StringMap;
class Widget;

/**
 * Check if the resource described by the specified headers can be
 * processed by the text processor.
 */
[[gnu::pure]]
bool
text_processor_allowed(const StringMap &headers) noexcept;

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
UnusedIstreamPtr
text_processor(struct pool &pool, UnusedIstreamPtr istream,
	       const Widget &widget, const WidgetContext &ctx) noexcept;
