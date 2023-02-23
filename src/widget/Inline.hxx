// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Query a widget and embed its HTML text after processing.
 */

#pragma once

#include "event/Chrono.hxx"

struct pool;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class UnusedIstreamPtr;
class Widget;
class StopwatchPtr;

extern const Event::Duration inline_widget_body_timeout;

/**
 * Utility function for the HTML processor which prepares a widget for
 * inlining into a HTML template.
 *
 * It requests the specified widget and formats the response in a way
 * that is suitable for embedding in HTML.
 *
 * @param plain_text expect text/plain?
 */
UnusedIstreamPtr
embed_inline_widget(struct pool &pool, SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    bool plain_text,
		    Widget &widget) noexcept;
