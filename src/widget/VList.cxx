// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "VList.hxx"
#include "View.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringAPI.hxx"

#include <algorithm> // for std::any_of()

WidgetViewList
Clone(AllocatorPtr alloc, const WidgetViewList &src) noexcept
{
	WidgetViewList dest;
	auto i = dest.before_begin();

	for (const auto &s : src) {
		WidgetView *p = s.Clone(alloc);
		i = dest.insert_after(i, *p);
	}

	return dest;

}

const WidgetView *
FindByName(const WidgetViewList &list, const char *name) noexcept
{
	assert(!list.empty());
	assert(list.front().name == nullptr);

	auto view = list.begin();
	assert(view != list.end());

	if (name == nullptr || *name == 0)
		/* the default view has no name */
		return &*view;

	for (++view; view != list.end(); ++view) {
		assert(view->name != nullptr);

		if (StringIsEqual(view->name, name))
			return &*view;
	}

	return nullptr;
}

bool
IsAnyExpandable(const WidgetViewList &list) noexcept
{
	return std::any_of(list.begin(), list.end(), [](const WidgetView &view){
		return view.IsExpandable();
	});
}

void
Expand(AllocatorPtr alloc, WidgetViewList &list,
       const MatchData &match_data) noexcept
{
	for (auto &i : list)
		i.Expand(alloc, match_data);
}
