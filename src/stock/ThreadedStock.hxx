// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <forward_list>
#include <mutex>

/**
 * A container which manages equivalent reusable instances of a type,
 * e.g. database connections.
 */
template<typename T>
class ThreadedStock {
	using Item = T;
	using List = std::forward_list<T>;

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
			const std::scoped_lock lock{mutex};
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
		const std::scoped_lock lock{mutex};
		items.splice_after(items.before_begin(), std::move(src));
	}
};
