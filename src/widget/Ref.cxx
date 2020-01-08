/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Ref.hxx"
#include "pool/pool.hxx"
#include "util/IterableSplitString.hxx"

#include <assert.h>
#include <string.h>

const WidgetRef *
widget_ref_parse(struct pool *pool, const char *_p)
{
	const WidgetRef *root = nullptr, **wr_p = &root;

	if (_p == nullptr || *_p == 0)
		return nullptr;

	char *p = p_strdup(pool, _p);

	for (auto id : IterableSplitString(p, WIDGET_REF_SEPARATOR)) {
		if (id.empty())
			continue;

		char *_id = const_cast<char *>(id.data);
		_id[id.size] = 0;

		auto wr = NewFromPool<WidgetRef>(*pool);
		wr->next = nullptr;
		wr->id = _id;

		*wr_p = wr;
		wr_p = &wr->next;
	}

	return root;
}

bool
widget_ref_includes(const WidgetRef *outer,
		    const WidgetRef *inner)
{
	assert(inner != nullptr);

	while (true) {
		if (strcmp(outer->id, inner->id) != 0)
			return false;

		outer = outer->next;
		if (outer == nullptr)
			return true;

		inner = inner->next;
		if (inner == nullptr)
			return false;
	}
}
