/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "util/StringSet.hxx"
#include "AllocatorPtr.hxx"

void
StringSet::Add(AllocatorPtr alloc, const char *p)
{
    auto *item = alloc.New<Item>();
    item->value = p;
    list.push_front(*item);
}

void
StringSet::CopyFrom(AllocatorPtr alloc, const StringSet &s)
{
    for (auto i : s)
        Add(alloc, alloc.Dup(i));
}
