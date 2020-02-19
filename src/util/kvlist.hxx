/*
 * Copyright 2007-2020 CM4all GmbH
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

#pragma once

#include "util/Compiler.h"

#include <boost/intrusive/slist.hpp>

/**
 * List of key/value pairs.
 */
class KeyValueList {
public:
	struct Item : boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
		const char *key, *value;

		Item(const char *_key, const char *_value)
			:key(_key), value(_value) {}
	};

private:
	typedef boost::intrusive::slist<Item,
					boost::intrusive::constant_time_size<false>> List;

	typedef List::const_iterator const_iterator;

	List list;

public:
	KeyValueList() = default;
	KeyValueList(const KeyValueList &) = delete;
	KeyValueList(KeyValueList &&src) {
		list.swap(src.list);
	}

	template<typename Alloc>
	KeyValueList(Alloc &&alloc, const KeyValueList &src) {
		for (const auto &i : src)
			Add(alloc, alloc.Dup(i.key), alloc.Dup(i.value));
	}

	KeyValueList &operator=(KeyValueList &&src) {
		list.swap(src.list);
		return *this;
	}

	const_iterator begin() const {
		return list.begin();
	}

	const_iterator end() const {
		return list.end();
	}

	gcc_pure
	bool IsEmpty() const {
		return list.empty();
	}

	void Clear() {
		list.clear();
	}

	template<typename Alloc>
	void Add(Alloc &&alloc, const char *key, const char *value) {
		auto item = alloc.template New<Item>(key, value);
		list.push_front(*item);
	}

	void Reverse() {
		list.reverse();
	}
};
