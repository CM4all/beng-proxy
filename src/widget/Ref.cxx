// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Ref.hxx"
#include "AllocatorPtr.hxx"
#include "util/IterableSplitString.hxx"
#include "util/StringAPI.hxx"

#include <assert.h>

const WidgetRef *
widget_ref_parse(AllocatorPtr alloc, const char *_p) noexcept
{
	const WidgetRef *root = nullptr, **wr_p = &root;

	if (_p == nullptr || *_p == 0)
		return nullptr;

	char *p = alloc.Dup(_p);

	for (auto id : IterableSplitString(p, WIDGET_REF_SEPARATOR)) {
		if (id.empty())
			continue;

		char *_id = const_cast<char *>(id.data());
		_id[id.size()] = 0;

		auto wr = alloc.New<WidgetRef>();
		wr->next = nullptr;
		wr->id = _id;

		*wr_p = wr;
		wr_p = &wr->next;
	}

	return root;
}

bool
widget_ref_includes(const WidgetRef *outer,
		    const WidgetRef *inner) noexcept
{
	assert(inner != nullptr);

	while (true) {
		if (!StringIsEqual(outer->id, inner->id))
			return false;

		outer = outer->next;
		if (outer == nullptr)
			return true;

		inner = inner->next;
		if (inner == nullptr)
			return false;
	}
}
