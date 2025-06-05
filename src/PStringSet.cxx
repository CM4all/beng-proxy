// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "util/StringSet.hxx"
#include "AllocatorPtr.hxx"

void
StringSet::Add(AllocatorPtr alloc, const char *p) noexcept
{
	auto *item = alloc.New<Item>();
	item->value = p;
	list.push_front(*item);
}

void
StringSet::CopyFrom(AllocatorPtr alloc, const StringSet &s) noexcept
{
	for (auto i : s)
		Add(alloc, alloc.Dup(i));
}
