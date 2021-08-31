/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "stock/Request.hxx"
#include "lease.hxx"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

class LeasePtr;
class StockMap;
class StockGetHandler;
class Stock;
struct StockItem;
struct StockStats;

/**
 * A #StockMap wrapper which allows multiple clients to use one
 * #StockItem.
 */
class MultiStock {
	class MapItem;

	class Item
		: public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>
	{

		struct Lease final : ::Lease {
			static constexpr auto link_mode = boost::intrusive::normal_link;
			using LinkMode = boost::intrusive::link_mode<link_mode>;
			using SiblingsListHook = boost::intrusive::list_member_hook<LinkMode>;

			SiblingsListHook siblings;

			using SiblingsListMemberHook =
				boost::intrusive::member_hook<Lease,
							      Lease::SiblingsListHook,
							      &Lease::siblings>;

			Item &item;

			Lease(Item &_item):item(_item) {}

			/* virtual methods from class Lease */
			void ReleaseLease(bool _reuse) noexcept override;
		};

		MapItem &parent;

		StockItem &item;

		boost::intrusive::list<Lease, Lease::SiblingsListMemberHook,
				       boost::intrusive::constant_time_size<false>> leases;

		unsigned remaining_leases;

		bool reuse = true;

	public:
		Item(MapItem &_parent, StockItem &_item,
		     unsigned _max_leases) noexcept
			:parent(_parent), item(_item),
			 remaining_leases(_max_leases) {}

		Item(const Item &) = delete;
		Item &operator=(const Item &) = delete;

		~Item() noexcept;

		bool IsFull() const noexcept {
			return remaining_leases == 0;
		}

		bool IsEmpty() const noexcept {
			return leases.empty();
		}

		bool CanUse() const noexcept {
			return reuse && !IsFull();
		}

		void Fade() noexcept {
			reuse = false;
		}

		template<typename P>
		void FadeIf(P &&predicate) noexcept {
			if (predicate(item))
				Fade();
		}

	private:
		Lease &AddLease() noexcept {
			Lease *lease = new Lease(*this);
			leases.push_front(*lease);
			--remaining_leases;
			return *lease;
		}

	public:
		void AddLease(StockGetHandler &handler,
			      LeasePtr &lease_ref) noexcept;

		StockItem *AddLease(LeasePtr &lease_ref) noexcept {
			lease_ref.Set(AddLease());
			return &item;
		}

		void DeleteLease(Lease *lease, bool _reuse) noexcept;
	};

	class MapItem final
		: public boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
	{
		Stock &stock;

		using ItemList =
			boost::intrusive::list<Item,
					       boost::intrusive::constant_time_size<false>>;
		ItemList items;

	public:
		explicit MapItem(Stock &_stock) noexcept
			:stock(_stock) {}

		Item &GetNow(StockRequest request, unsigned max_leases);

		void RemoveItem(Item &item) noexcept;

		void FadeAll() noexcept {
			for (auto &i : items)
				i.Fade();
		}

		template<typename P>
		void FadeIf(P &&predicate) noexcept {
			for (auto &i : items)
				i.FadeIf(std::forward<P>(predicate));
		}

	private:
		[[gnu::pure]]
		Item *FindUsable() noexcept;

	public:
		struct Hash {
			[[gnu::pure]]
			std::size_t operator()(const char *key) const noexcept;

			[[gnu::pure]]
			std::size_t operator()(const MapItem &value) const noexcept;
		};

		struct Equal {
			[[gnu::pure]]
			bool operator()(const char *a, const MapItem &b) const noexcept;

			[[gnu::pure]]
			bool operator()(const MapItem &a, const MapItem &b) const noexcept;
		};
	};

	StockMap &hstock;

	using Map =
		boost::intrusive::unordered_set<MapItem,
						boost::intrusive::hash<MapItem::Hash>,
						boost::intrusive::equal<MapItem::Equal>,
						boost::intrusive::constant_time_size<false>>;

	static constexpr size_t N_BUCKETS = 251;
	Map::bucket_type buckets[N_BUCKETS];

	Map map;

public:
	explicit MultiStock(StockMap &_hstock) noexcept;
	~MultiStock() noexcept;

	MultiStock(const MultiStock &) = delete;
	MultiStock &operator=(const MultiStock &) = delete;

	/**
	 * @see Stock::FadeAll()
	 */
	void FadeAll() noexcept {
		for (auto &i : map)
			i.FadeAll();
	}

	/**
	 * @see Stock::FadeIf()
	 */
	template<typename P>
	void FadeIf(P &&predicate) noexcept {
		for (auto &i : map)
			i.FadeIf(std::forward<P>(predicate));
	}

	/**
	 * Obtains an item from the stock without going through the
	 * callback.  This requires a stock class which finishes the
	 * StockClass::Create() method immediately.
	 *
	 * Throws exception on error.
	 *
	 * @param max_leases the maximum number of leases per stock_item
	 */
	StockItem *GetNow(const char *uri, StockRequest request,
			  unsigned max_leases,
			  LeasePtr &lease_ref);

private:
	[[gnu::pure]]
	MapItem &MakeMapItem(const char *uri, void *request) noexcept;
};
