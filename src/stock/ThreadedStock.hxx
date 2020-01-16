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

#include <forward_list>
#include <mutex>

/**
 * A container which manages equivalent reusable instances of a type,
 * e.g. database connections.
 */
template<typename T>
class ThreadedStock {
	typedef T Item;
	typedef std::forward_list<T> List;

	std::mutex mutex;

	/**
	 * A list of idle items.
	 */
	List items;

	class Lease {
		ThreadedStock<T> &stock;

		List items;

	public:
		Lease(ThreadedStock<T> &_stock, List &&_list)
			:stock(_stock), items(std::move(_list)) {}

		Lease(Lease &&src)
			:stock(src.stock) {
			items.swap(src.items);
		}

		~Lease() {
			stock.Put(std::move(items));
		}

		T &operator*() {
			return items.front();
		}

		T *operator->() {
			return &items.front();
		}
	};

public:
	template<typename... Args>
	Lease Get(Args&&... args) {
		List tmp;

		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!items.empty())
				tmp.splice_after(tmp.before_begin(),
						 items, items.before_begin());
		}

		if (tmp.empty())
			tmp.emplace_front(std::forward<Args>(args)...);

		return Lease(*this, std::move(tmp));
	}

private:
	void Put(List &&src) {
		const std::unique_lock<std::mutex> lock(mutex);
		items.splice_after(items.before_begin(), std::move(src));
	}
};
