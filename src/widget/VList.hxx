// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveForwardList.hxx"

class AllocatorPtr;
class MatchData;
struct WidgetView;

using WidgetViewList = IntrusiveForwardList<WidgetView>;

WidgetViewList
Clone(AllocatorPtr alloc, const WidgetViewList &src) noexcept;

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
[[gnu::pure]]
const WidgetView *
FindByName(const WidgetViewList &list, const char *name) noexcept;

/**
 * Does any view in the linked list need to be expanded with
 * widget_view_expand()?
 */
[[gnu::pure]]
bool
IsAnyExpandable(const WidgetViewList &list) noexcept;

/**
 * The same as widget_view_expand(), but expand all voews in
 * the linked list.
 */
void
Expand(AllocatorPtr alloc, WidgetViewList &list,
       const MatchData &match_data) noexcept;
