// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>
#include <utility>

#include <stddef.h>

struct pool;
class UnusedIstreamPtr;
struct SubstNode;

class SubstTree {
	SubstNode *root = nullptr;

public:
	SubstTree() = default;

	SubstTree(SubstTree &&src) noexcept
		:root(std::exchange(src.root, nullptr)) {}

	SubstTree &operator=(SubstTree &&src) noexcept {
		using std::swap;
		swap(root, src.root);
		return *this;
	}

	bool Add(struct pool &pool, const char *a0, std::string_view b) noexcept;
	bool Add(struct pool &pool, const char *a0, const char *b) noexcept;

	[[gnu::pure]]
	std::pair<const SubstNode *, const char *> FindFirstChar(const char *data,
								 size_t length) const noexcept;
};

/**
 * This istream filter substitutes a word with another string.
 */
UnusedIstreamPtr
istream_subst_new(struct pool *pool, UnusedIstreamPtr input,
		  SubstTree tree) noexcept;
