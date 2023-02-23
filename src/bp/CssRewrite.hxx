// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

struct pool;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class StopwatchPtr;
class UnusedIstreamPtr;
struct escape_class;
class Widget;

/**
 * Rewrite URLs in CSS.
 *
 * @return NULL if no rewrite is necessary
 */
UnusedIstreamPtr
css_rewrite_block_uris(struct pool &pool,
		       SharedPoolPtr<WidgetContext> ctx,
		       const StopwatchPtr &parent_stopwatch,
		       Widget &widget,
		       std::string_view block,
		       const struct escape_class *escape) noexcept;
