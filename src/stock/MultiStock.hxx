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
#include "stock/GetHandler.hxx"
#include "stock/AbstractStock.hxx"
#include "stock/Stock.hxx"
#include "event/DeferEvent.hxx"
#include "event/TimerEvent.hxx"
#include "util/Cancellable.hxx"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/unordered_set.hpp>

class CancellablePointer;
class StockClass;
struct CreateStockItem;
struct StockItem;
struct StockStats;

class MultiStockClass {
public:
	virtual Event::Duration GetClearInterval(void *info) const noexcept = 0;

	virtual StockItem *Create(CreateStockItem c,
				  StockItem &shared_item) = 0;
};

/**
 * A #StockMap wrapper which allows multiple clients to use one
 * #StockItem.
 */
class MultiStock {
	class MapItem;

	/**
	 * A manager for an "outer" #StockItem which can be shared by
	 * multiple clients.
	 */
	class OuterItem final
		: public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
		  AbstractStock
	{
		MapItem &parent;

		StockItem &shared_item;

		std::size_t limit;

		/**
		 * This timer periodically deletes one third of all
		 * idle items, to get rid of all unused items
		 * eventually.
		 */
		TimerEvent cleanup_timer;

		using ItemList =
			boost::intrusive::list<StockItem,
					       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
					       boost::intrusive::constant_time_size<true>>;

		ItemList idle, busy;

	public:
		OuterItem(MapItem &_parent, StockItem &_item,
			  std::size_t _limit) noexcept;

		OuterItem(const OuterItem &) = delete;
		OuterItem &operator=(const OuterItem &) = delete;

		~OuterItem() noexcept;

		bool IsFull() const noexcept {
			return busy.size() >= limit;
		}

		bool IsBusy() const noexcept {
			return !busy.empty();
		}

		bool IsEmpty() const noexcept {
			return idle.empty() && busy.empty();
		}

		bool CanUse() const noexcept;
		bool ShouldDelete() const noexcept;

		void DiscardUnused() noexcept;

		void Fade() noexcept;

		template<typename P>
		void FadeIf(P &&predicate) noexcept {
			if (predicate(shared_item))
				Fade();
		}

		void CreateLease(MultiStockClass &_cls,
				 StockGetHandler &handler) noexcept;

		void GetLease(MultiStockClass &_cls,
			      StockGetHandler &handler) noexcept;

	private:
		StockItem *GetIdle() noexcept;

		bool GetIdle(StockGetHandler &handler) noexcept;

		void OnCleanupTimer() noexcept;

		void ScheduleCleanupTimer() noexcept {
			cleanup_timer.Schedule(std::chrono::minutes{5});
		}

		void ScheduleCleanupNow() noexcept {
			cleanup_timer.Schedule(Event::Duration::zero());
		}

		void CancelCleanupTimer() noexcept {
			cleanup_timer.Cancel();
		}

		/* virtual methods from class AbstractStock */
		const char *GetName() const noexcept override;
		EventLoop &GetEventLoop() const noexcept override;

		void Put(StockItem &item, bool destroy) noexcept override;

		void ItemIdleDisconnect(StockItem &item) noexcept override;
		void ItemBusyDisconnect(StockItem &item) noexcept override;
		void ItemCreateSuccess(StockItem &item) noexcept override;
		void ItemCreateError(StockGetHandler &get_handler,
				     std::exception_ptr ep) noexcept override;
		void ItemCreateAborted() noexcept override;
		void ItemUncleanFlagCleared() noexcept override;
	};

	class MapItem final
		: public boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>,
		  Stock,
		  StockGetHandler
	{
		MultiStockClass &inner_class;

		using OuterItemList =
			boost::intrusive::list<OuterItem,
					       boost::intrusive::constant_time_size<false>>;
		OuterItemList items;

		struct Waiting;
		using WaitingList =
			boost::intrusive::list<Waiting,
					       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
					       boost::intrusive::constant_time_size<false>>;

		WaitingList waiting;

		/**
		 * This event is used to move the "retry_waiting" code
		 * out of the current stack, to invoke the handler
		 * method in a safe environment.
		 */
		DeferEvent retry_event;

		CancellablePointer get_cancel_ptr;

		std::size_t get_concurrency;

	public:
		MapItem(EventLoop &event_loop, StockClass &_outer_class,
			const char *_name,
			std::size_t _limit, std::size_t _max_idle,
			Event::Duration _clear_interval,
			MultiStockClass &_inner_class) noexcept;
		~MapItem() noexcept;

		bool IsEmpty() noexcept {
			return items.empty() && waiting.empty();
		}

		void Get(StockRequest request, std::size_t concurrency,
			 StockGetHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept;

		void RemoveWaiting(Waiting &w) noexcept;

		void RemoveItem(OuterItem &item) noexcept;
		void OnLeaseReleased(OuterItem &item) noexcept;

		void DiscardUnused() noexcept;

		void FadeAll() noexcept {
			for (auto &i : items)
				i.Fade();
		}

		template<typename P>
		void FadeIf(P &&predicate) noexcept {
			for (auto &i : items)
				i.FadeIf(predicate);
		}

	private:
		[[gnu::pure]]
		OuterItem *FindUsable() noexcept;

		/**
		 * Delete all empty items.
		 */
		void DeleteEmptyItems(const OuterItem *except=nullptr) noexcept;

		void FinishWaiting(OuterItem &item) noexcept;

		/**
		 * Retry the waiting requests.
		 */
		void RetryWaiting() noexcept;
		bool ScheduleRetryWaiting() noexcept;

		/* virtual methods from class StockGetHandler */
		void OnStockItemReady(StockItem &item) noexcept override;
		void OnStockItemError(std::exception_ptr error) noexcept override;

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

	EventLoop &event_loop;

	StockClass &outer_class;

	/**
	 * The maximum number of items in each stock.
	 */
	const std::size_t limit;

	/**
	 * The maximum number of permanent idle items in each stock.
	 */
	const std::size_t max_idle;

	MultiStockClass &inner_class;

	using Map =
		boost::intrusive::unordered_set<MapItem,
						boost::intrusive::hash<MapItem::Hash>,
						boost::intrusive::equal<MapItem::Equal>,
						boost::intrusive::constant_time_size<false>>;

	static constexpr size_t N_BUCKETS = 251;
	Map::bucket_type buckets[N_BUCKETS];

	Map map;

public:
	MultiStock(EventLoop &_event_loop, StockClass &_outer_cls,
		   std::size_t _limit, std::size_t _max_idle,
		   MultiStockClass &_inner_class) noexcept;
	~MultiStock() noexcept;

	MultiStock(const MultiStock &) = delete;
	MultiStock &operator=(const MultiStock &) = delete;

	auto &GetEventLoop() const noexcept {
		return event_loop;
	}

	/**
	 * Discard all items which are idle and havn't been used in a
	 * while.
	 */
	void DiscardUnused() noexcept;

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
			i.FadeIf(predicate);
	}

	void Get(const char *uri, StockRequest request,
		 std::size_t concurrency,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	[[gnu::pure]]
	MapItem &MakeMapItem(const char *uri, void *request) noexcept;
};
